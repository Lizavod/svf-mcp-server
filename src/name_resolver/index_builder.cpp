#include "name_resolver/index_builder.hpp"

#include "name_resolver/utils.hpp"

#include "SVFIR/SVFIR.h"
#include "SVFIR/SVFValue.h"
#include "Util/SVFUtil.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace svf_engine {

bool build_index_from_result(const std::string& work_dir, const std::string& index_path,
                             std::vector<std::string>& name_index) {
    utils::StdoutToStderr redirect;

    try {
        const std::filesystem::path dir(work_dir);
        const std::filesystem::path graph_csv = dir / "graph.csv";
        const std::filesystem::path meta_csv = dir / "graph_meta.csv";
        const std::filesystem::path solver_result = dir / "solver_result.txt";

        // Store pairs (src, dst) that have a direct "assign" edge in the SVFG.
        std::set<std::pair<SVF::NodeID, SVF::NodeID>> direct_assign_edges;
        std::ifstream graph_in(graph_csv);
        if (!graph_in.is_open()) {
            std::cerr << "[SVF ERROR] Failed to open graph file: " << graph_csv << std::endl;
            return false;
        }

        std::string g_line;
        while (std::getline(graph_in, g_line)) {
            if (g_line.empty()) {
                continue;
            }

            std::istringstream row(g_line);
            SVF::NodeID s = 0;
            SVF::NodeID d = 0;
            std::string label;
            if (row >> s >> d >> label && label == "assign") {
                direct_assign_edges.insert({s, d});
            }
        }

        std::map<SVF::NodeID, utils::NodeMeta> node_meta;
        if (!utils::load_node_meta(meta_csv.string(), node_meta)) {
            std::cerr << "[SVF ERROR] Failed to read graph metadata." << std::endl;
            return false;
        }

        std::ifstream result_file(solver_result);
        if (!result_file.is_open()) {
            std::cerr << "[SVF ERROR] Failed to open solver result file." << std::endl;
            return false;
        }

        SVF::SVFIR* pag = SVF::SVFIR::getPAG();
        if (!pag) {
            std::cerr << "[SVF ERROR] PAG is not available." << std::endl;
            return false;
        }

        name_index.clear();
        std::set<std::string> unique_rows;
        std::map<std::string, std::set<std::string>> aggregated_data;

        std::string line;
        while (std::getline(result_file, line)) {
            std::istringstream row(line);
            long long src_raw = -1;
            long long dst_raw = -1;
            if (!(row >> src_raw >> dst_raw)) {
                continue;
            }

            const SVF::NodeID src_id = static_cast<SVF::NodeID>(src_raw);
            const SVF::NodeID dst_id = static_cast<SVF::NodeID>(dst_raw);

            // 1. Метаданные
            utils::NodeMeta src_meta;
            utils::NodeMeta dst_meta;
            if (auto it = node_meta.find(src_id); it != node_meta.end()) {
                src_meta = it->second;
            }
            if (auto it = node_meta.find(dst_id); it != node_meta.end()) {
                dst_meta = it->second;
            }

            // 2. Жесткий фильтр служебных узлов
            auto is_service_node = [](const std::string& name) {
                if (name.empty())
                    return true;
                if (name.rfind("node_", 0) == 0)
                    return true;
                if (name[0] == '_')
                    return true;
                if (name.find("(stack base object)") != std::string::npos)
                    return true;
                if (name.find("(base object)") != std::string::npos)
                    return true;
                if (name.find("(heap base object)") != std::string::npos)
                    return true;
                if (name == "main_ret" || name == "dummyVal" || name == "retval")
                    return true;
                if (name.find("call") == 0)
                    return true;
                return false;
            };

            if (is_service_node(src_meta.name) || is_service_node(dst_meta.name)) {
                continue;
            }

            // 3. БЕЗОПАСНАЯ проверка на поля
            bool src_is_field = false;
            bool dst_is_field = false;

            if (pag->hasGNode(src_id)) {
                const SVF::SVFVar* src_var = pag->getSVFVar(src_id);
                if (src_var) {
                    src_is_field = SVF::SVFUtil::isa<SVF::GepValVar>(src_var) ||
                                   SVF::SVFUtil::isa<SVF::GepObjVar>(src_var);
                }
            }
            if (pag->hasGNode(dst_id)) {
                const SVF::SVFVar* dst_var = pag->getSVFVar(dst_id);
                if (dst_var) {
                    dst_is_field = SVF::SVFUtil::isa<SVF::GepValVar>(dst_var) ||
                                   SVF::SVFUtil::isa<SVF::GepObjVar>(dst_var);
                }
            }

            // 4. ФИЛЬТР ФАНТОМНЫХ ПУТЕЙ
            const bool has_direct_assign = direct_assign_edges.count({src_id, dst_id}) > 0;
            if (!src_is_field && !dst_is_field && !has_direct_assign &&
                src_meta.name != dst_meta.name) {
                continue;
            }

            // 5. Выбор имени
            std::string variable = "";
            if (src_is_field) {
                variable = src_meta.name;
            } else if (dst_is_field) {
                variable = dst_meta.name;
            } else {
                variable = src_meta.name;
                if (variable.empty() || variable.rfind("node_", 0) == 0) {
                    variable = dst_meta.name;
                }
            }

            if (variable.empty()) {
                continue;
            }

            // 6. Агрегация
            std::string key =
                "Variable: " + variable + " | Defined: " + utils::format_location(src_meta);

            std::string use_loc = utils::format_location(dst_meta);
            aggregated_data[key].insert(use_loc);
        }

        std::map<std::string, std::pair<std::string, std::set<std::string>>> final_data;

        for (const auto& [key, uses] : aggregated_data) {
            // Извлекаем имя переменной из ключа (формат: "Variable: tmp | Defined: ...")
            size_t start = key.find("Variable: ") + 10;
            size_t end = key.find(" | Defined:");
            std::string var_name = key.substr(start, end - start);

            std::string def_line = key.substr(end + 12); // строка определения

            if (final_data.find(var_name) == final_data.end()) {
                // Первое встреченное определение
                final_data[var_name] = {def_line, uses};
            } else {
                // Если новое определение раньше по строке, обновляем его
                try {
                    int new_line = std::stoi(def_line.substr(def_line.rfind(':') + 1));
                    int old_line = std::stoi(
                        final_data[var_name].first.substr(final_data[var_name].first.rfind(':') + 1));
                    if (new_line < old_line) {
                        final_data[var_name].first = def_line;
                    }
                } catch (...) {
                }

                // Объединяем использования
                final_data[var_name].second.insert(uses.begin(), uses.end());
            }
        }

        // Формируем итоговый вектор
        name_index.clear();
        for (const auto& [var_name, data] : final_data) {
            std::string uses_str = "";
            for (const auto& use : data.second) {
                if (!uses_str.empty())
                    uses_str += ", ";
                uses_str += use;
            }
            name_index.push_back("Variable: " + var_name + " | Defined: " + data.first +
                                 " | Used: " + uses_str);
        }

        std::ofstream index_file_out(index_path);
        if (!index_file_out.is_open()) {
            std::cerr << "[SVF ERROR] Failed to open index file for writing: " << index_path
                      << std::endl;
            return false;
        }
        for (const std::string& entry : name_index) {
            index_file_out << entry << '\n';
        }
        index_file_out.close();

        std::cerr << "[SVF INFO] Built CFL index with " << name_index.size() << " entries at "
                  << index_path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[SVF ERROR] Failed to build index from result: " << e.what() << std::endl;
        return false;
    }
}

} // namespace svf_engine
