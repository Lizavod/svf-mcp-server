#include "svf_engine/engine.hpp"
#include "Graphs/ICFGNode.h"
#include "Graphs/SVFG.h"
#include "MSSA/SVFGBuilder.h"
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "SVFIR/SVFIR.h"
#include "SVFIR/SVFValue.h"
#include "SVFIR/SVFStatements.h"
#include "Util/SVFUtil.h"
#include "Util/Options.h"
#include "WPA/Andersen.h"
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/types.h>
#include <unistd.h>

namespace svf_engine {

namespace {

std::string csv_escape(const std::string& field) {
    if (field.find_first_of(",\"\n\r") == std::string::npos) {
        return field;
    }

    std::string escaped = "\"";
    for (char c : field) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += '"';
    return escaped;
}

std::string edge_kind_name(SVF::SVFStmt::PEDGEK kind) {
    switch (kind) {
        case SVF::SVFStmt::Addr:
            return "Addr";
        case SVF::SVFStmt::Copy:
            return "Copy";
        case SVF::SVFStmt::Store:
            return "Store";
        case SVF::SVFStmt::Load:
            return "Load";
        case SVF::SVFStmt::Call:
            return "Call";
        case SVF::SVFStmt::Ret:
            return "Ret";
        case SVF::SVFStmt::Gep:
            return "Gep";
        case SVF::SVFStmt::Phi:
            return "Phi";
        case SVF::SVFStmt::Select:
            return "Select";
        case SVF::SVFStmt::Cmp:
            return "Cmp";
        case SVF::SVFStmt::BinaryOp:
            return "BinaryOp";
        case SVF::SVFStmt::UnaryOp:
            return "UnaryOp";
        case SVF::SVFStmt::Branch:
            return "Branch";
        case SVF::SVFStmt::ThreadFork:
            return "ThreadFork";
        case SVF::SVFStmt::ThreadJoin:
            return "ThreadJoin";
    }

    return "Unknown";
}

std::string clean_name(const std::string& raw) {
    size_t begin = 0;
    while (begin < raw.size() && std::isspace(static_cast<unsigned char>(raw[begin]))) {
        ++begin;
    }
    const std::string name = raw.substr(begin);

    if (name.empty() || name.find("dummy") != std::string::npos || name.find("node_") == 0 ||
        name.find("call") == 0 || name.find("(stack") == 0 || name.find("(heap") == 0 ||
        name.find("retval") != std::string::npos) {
        return "";
    }

    const size_t dot_pos = name.find('.');
    if (dot_pos != std::string::npos) {
        return name.substr(0, dot_pos);
    }
    return name;
}

std::string node_name(const SVF::SVFIR* pag, SVF::NodeID id) {
    const SVF::SVFVar* var = pag->getSVFVar(id);
    if (var) {
        std::string name = var->getValueName();
        if (!name.empty()) {
            return name;
        }
    }

    return "node_" + std::to_string(id);
}

bool json_field(const std::string& raw, const std::string& key, std::string& value) {
    const std::string pattern = "\"" + key + "\"";
    size_t pos = raw.find(pattern);
    if (pos == std::string::npos) {
        return false;
    }

    pos = raw.find(':', pos + pattern.size());
    if (pos == std::string::npos) {
        return false;
    }
    ++pos;

    while (pos < raw.size() && std::isspace(static_cast<unsigned char>(raw[pos]))) {
        ++pos;
    }
    if (pos >= raw.size()) {
        return false;
    }

    if (raw[pos] == '"') {
        ++pos;
        std::string result;
        while (pos < raw.size() && raw[pos] != '"') {
            if (raw[pos] == '\\' && pos + 1 < raw.size()) {
                result += raw[pos + 1];
                pos += 2;
            } else {
                result += raw[pos++];
            }
        }
        value = result;
    } else {
        const size_t start = pos;
        while (pos < raw.size() &&
               (std::isdigit(static_cast<unsigned char>(raw[pos])) || raw[pos] == '-')) {
            ++pos;
        }
        value = raw.substr(start, pos - start);
    }

    return !value.empty();
}

std::string compact_source_loc(const std::string& raw) {
    std::string file;
    std::string line;

    const bool has_file = json_field(raw, "fl", file) || json_field(raw, "file", file);
    const bool has_line = json_field(raw, "ln", line);

    if (has_file && has_line) {
        return file + ":" + line;
    }
    if (has_file) {
        return file;
    }
    if (has_line) {
        return line;
    }

    return raw;
}

struct NodeMeta {
    std::string name;
    std::string file;
    std::string line;
};

void split_source_loc(const std::string& raw, std::string& file, std::string& line) {
    file.clear();
    line.clear();

    if (!json_field(raw, "fl", file)) {
        json_field(raw, "file", file);
    }
    if (!json_field(raw, "ln", line)) {
        json_field(raw, "line", line);
    }

    if (file.empty() && line.empty()) {
        const size_t colon = raw.rfind(':');
        if (colon != std::string::npos && colon + 1 < raw.size()) {
            bool digits = true;
            for (size_t i = colon + 1; i < raw.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(raw[i]))) {
                    digits = false;
                    break;
                }
            }
            if (digits) {
                file = raw.substr(0, colon);
                line = raw.substr(colon + 1);
            }
        }
    }
}

std::string format_location(const NodeMeta& meta) {
    if (!meta.file.empty() && !meta.line.empty()) {
        return meta.file + ":" + meta.line;
    }
    if (!meta.file.empty()) {
        return meta.file;
    }
    if (!meta.line.empty()) {
        return meta.line;
    }
    return "unknown";
}

std::string svfg_node_name(const SVF::SVFG* svfg, const SVF::SVFGNode* node) {
    if (!node) {
        return "node_unknown";
    }

    if (const SVF::SVFVar* var = node->getValue()) {
        const std::string value_name = var->getValueName();
        if (!value_name.empty()) {
            return value_name;
        }
    }

    const SVF::SVFIR* pag = (svfg && svfg->getPTA()) ? svfg->getPTA()->getPAG() : nullptr;
    if (pag) {
        const SVF::NodeBS defs = node->getDefSVFVars();
        for (SVF::NodeID id : defs) {
            const SVF::SVFVar* var = pag->getSVFVar(id);
            if (var && !var->getValueName().empty()) {
                return var->getValueName();
            }
        }
    }

    if (const SVF::ICFGNode* icfg = node->getICFGNode()) {
        for (const SVF::SVFStmt* stmt : icfg->getSVFStmts()) {
            if (!stmt) {
                continue;
            }
            const SVF::SVFVar* var = stmt->getDstNode();
            if (var && !var->getValueName().empty()) {
                return var->getValueName();
            }
            var = stmt->getSrcNode();
            if (var && !var->getValueName().empty()) {
                return var->getValueName();
            }
        }
    }

    return "node_" + std::to_string(node->getId());
}

std::string vfg_edge_label(SVF::VFGEdge::VFGEdgeK kind) {
    switch (kind) {
        case SVF::VFGEdge::IntraDirectVF:
            return "assign";
        case SVF::VFGEdge::IntraIndirectVF:
            return "mem";
        case SVF::VFGEdge::CallDirVF:
        case SVF::VFGEdge::CallIndVF:
            return "call";
        case SVF::VFGEdge::RetDirVF:
        case SVF::VFGEdge::RetIndVF:
            return "ret";
        case SVF::VFGEdge::TheadMHPIndirectVF:
            return "mem";
    }
    return "mem";
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(field);
            field.clear();
        } else if (c != '\r') {
            field += c;
        }
    }
    fields.push_back(field);

    return fields;
}

bool parse_node_id(const std::string& value, SVF::NodeID& id) {
    if (value.empty()) {
        return false;
    }
    for (char c : value) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    try {
        id = static_cast<SVF::NodeID>(std::stoull(value));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool load_node_meta(const std::string& meta_path, std::map<SVF::NodeID, NodeMeta>& node_meta) {
    std::ifstream meta_file(meta_path);
    if (!meta_file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(meta_file, line)) {
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = split_csv_line(line);
        if (fields.size() < 4) {
            continue;
        }

        SVF::NodeID id = 0;
        if (!parse_node_id(fields[0], id)) {
            continue;
        }

        node_meta[id] = NodeMeta{fields[1], fields[2], fields[3]};
    }

    return true;
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

} // namespace

class StdoutToStderr {
  public:
    StdoutToStderr() {
        saved_stdout = dup(STDOUT_FILENO);
        dup2(STDERR_FILENO, STDOUT_FILENO);
    }

    ~StdoutToStderr() {
        fflush(stdout);
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }

  private:
    int saved_stdout;
};

Engine::Engine() = default;

Engine::~Engine() {
    svfg_builder_.reset();
    svfg_ = nullptr;

    if (is_loaded_) {
        std::cerr << "[SVF INFO] Releasing SVF resources..." << std::endl;
        SVF::AndersenWaveDiff::releaseAndersenWaveDiff();
        SVF::SVFIR::releaseSVFIR();
        SVF::LLVMModuleSet::releaseLLVMModuleSet();
    }
}

bool Engine::load_module(const std::string& bc_file_path) {

    StdoutToStderr redirect;

    try {
        svfg_builder_.reset();
        svfg_ = nullptr;
        name_index_.clear();

        if (is_loaded_) {
            SVF::AndersenWaveDiff::releaseAndersenWaveDiff();
            SVF::SVFIR::releaseSVFIR();
            SVF::LLVMModuleSet::releaseLLVMModuleSet();
        }

        std::cerr << "[SVF INFO] Loading module: " << bc_file_path << std::endl;

        std::vector<std::string> moduleNameVec = {bc_file_path};
        SVF::LLVMModuleSet::preProcessBCs(moduleNameVec);
        SVF::LLVMModuleSet::buildSVFModule(moduleNameVec);

        SVF::SVFIRBuilder builder;
        SVF::SVFIR* pag = builder.build();

        SVF::Andersen* ander = SVF::AndersenWaveDiff::createAndersenWaveDiff(pag);
        SVF::CallGraph* cg = ander->getCallGraph();

        svfg_builder_ = std::make_unique<SVF::SVFGBuilder>();
        svfg_ = svfg_builder_->buildFullSVFG(ander);
        if (!svfg_) {
            throw std::runtime_error("Failed to build the full SVFG");
        }

        function_names_.clear();
        for (auto it = cg->begin(); it != cg->end(); ++it) {
            SVF::CallGraphNode* node = it->second;
            if (node->getFunction()) {
                function_names_.push_back(std::string(node->getFunction()->getName()));
            } else {
                function_names_.push_back("ExternalNode_" + std::to_string(node->getId()));
            }
        }

        current_path_ = bc_file_path;
        is_loaded_ = true;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[SVF ERROR] Failed to load module: " << e.what() << std::endl;
        is_loaded_ = false;
        return false;
    }
}

std::vector<std::string> Engine::get_function_names() const {
    return function_names_;
}

bool Engine::prepare_artifacts(const std::string& output_dir) {
    if (!is_loaded_ || !svfg_) {
        std::cerr << "[SVF ERROR] No SVFG available; load a module first." << std::endl;
        return false;
    }

    StdoutToStderr redirect;

    try {
        const std::filesystem::path dir(output_dir);
        std::filesystem::create_directories(dir);

        const std::filesystem::path graph_path = dir / "graph.csv";
        const std::filesystem::path meta_path = dir / "graph_meta.csv";
        const std::filesystem::path grammar_path = dir / "grammar.cfg";

        std::ofstream graph_file(graph_path);
        std::ofstream meta_file(meta_path);
        std::ofstream grammar_file(grammar_path);
        if (!graph_file.is_open() || !meta_file.is_open() || !grammar_file.is_open()) {
            std::cerr << "[SVF ERROR] Failed to open SVFG artifact files in " << output_dir
                      << std::endl;
            return false;
        }

        std::set<SVF::NodeID> unique_objs;
        bool uses_unindexed_mem = false;

        meta_file << "node_id,name,file,line\n";

        std::map<SVF::NodeID, NodeMeta> node_meta;
        auto collect_meta = [&](SVF::NodeID id) {
            if (node_meta.find(id) != node_meta.end()) {
                return;
            }

            NodeMeta meta;
            const SVF::SVFGNode* node = svfg_->hasSVFGNode(id) ? svfg_->getSVFGNode(id) : nullptr;
            meta.name = node ? svfg_node_name(svfg_, node) : ("node_" + std::to_string(id));

            if (node) {
                if (const SVF::ICFGNode* icfg = node->getICFGNode()) {
                    split_source_loc(icfg->getSourceLoc(), meta.file, meta.line);
                    if (meta.file.empty() && meta.line.empty() && icfg->getFun()) {
                        split_source_loc(icfg->getFun()->getSourceLoc(), meta.file, meta.line);
                    }
                } else if (node->getFun()) {
                    split_source_loc(node->getFun()->getSourceLoc(), meta.file, meta.line);
                }
            }

            node_meta[id] = meta;
            meta_file << id << ',' << csv_escape(meta.name) << ',' << csv_escape(meta.file) << ','
                      << csv_escape(meta.line) << '\n';
        };

        for (auto it = svfg_->begin(); it != svfg_->end(); ++it) {
            SVF::SVFGNode* node = it->second;
            if (!node) {
                continue;
            }

            for (const SVF::SVFGEdge* edge : node->getOutEdges()) {
                if (!edge) {
                    continue;
                }

                const SVF::NodeID src_id = edge->getSrcID();
                const SVF::NodeID dst_id = edge->getDstID();
                const auto edge_kind =
                    static_cast<SVF::VFGEdge::VFGEdgeK>(edge->getEdgeKind());

                std::vector<std::string> labels;
                if (edge->isIndirectVFGEdge()) {
                    const auto* indirect_edge =
                        static_cast<const SVF::IndirectSVFGEdge*>(edge);
                    for (const SVF::NodeID obj : indirect_edge->getPointsTo()) {
                        unique_objs.insert(obj);
                        labels.push_back("mem_" + std::to_string(obj));
                    }

                    if (labels.empty()) {
                        const std::string fallback = vfg_edge_label(edge_kind);
                        if (fallback == "mem") {
                            uses_unindexed_mem = true;
                        }
                        labels.push_back(fallback);
                    }
                } else {
                    labels.push_back(vfg_edge_label(edge_kind));
                }

                collect_meta(src_id);
                collect_meta(dst_id);

                for (const std::string& label : labels) {
                    graph_file << src_id << ' ' << dst_id << ' ' << label << '\n';
                }
            }
        }
        graph_file.close();
        meta_file.close();

        // CNF-грамматика для матричного решателя (POCR формат)
        grammar_file << "S\tassign\tS\n";
        for (const SVF::NodeID obj : unique_objs) {
            grammar_file << "S\tmem_" << obj << "\tS\n";
        }
        if (uses_unindexed_mem) {
            grammar_file << "S\tmem\tS\n";
        }
        // Разбиваем call S ret на два правила (CNF требует 1 или 2 символа справа)
        grammar_file << "S\tcall\tCallRet\n";
        grammar_file << "CallRet\tS\tret\n";
        grammar_file << "S\tret\tS\n";
        grammar_file << "S\tS\tS\n";
        grammar_file << "S\n";           // epsilon-правило
        grammar_file << "Count:\n";
        grammar_file << "S\n";
        grammar_file.close();

        std::cerr << "[SVF INFO] Exported SVFG artifacts to " << output_dir << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[SVF ERROR] Failed to export SVFG artifacts: " << e.what() << std::endl;
        return false;
    }
}

bool Engine::build_cfl_index(const std::string& index_path) {
    if (!is_loaded_ || !svfg_) {
        std::cerr << "[SVF ERROR] No SVFG available; load a module first." << std::endl;
        return false;
    }

    StdoutToStderr redirect;

    try {
        const std::filesystem::path index_file(index_path);
        if (index_file.has_parent_path()) {
            std::filesystem::create_directories(index_file.parent_path());
        }

        const std::filesystem::path work_dir(index_path + ".artifacts");
        std::filesystem::create_directories(work_dir);

        if (!prepare_artifacts(work_dir.string())) {
            return false;
        }
        if (!run_solver(work_dir.string())) {
            return false;
        }
        if (!build_index_from_result(work_dir.string(), index_path)) {
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "[SVF ERROR] Failed to build CFL index: " << e.what() << std::endl;
        return false;
    }
}

bool Engine::run_solver(const std::string& work_dir) {
    StdoutToStderr redirect;

    try {
        std::filesystem::path abs_work_dir = std::filesystem::absolute(work_dir);
        std::string host_path = abs_work_dir.string();

        std::string container_graph = "/data/graph.csv";
        std::string container_grammar = "/data/grammar.cfg"; // или grammar_cnf.cfg
        std::string container_result = "/data/solver_result.txt";

        std::string command =
            "docker run --rm -v " + shell_quote(host_path + ":/data") + " "
            "cfpq/py_algo:latest "
            "python3 -m cfpq_cli.run_all_pairs_cflr "
            "NonIncrementalAllPairsCFLReachabilityMatrix "
            + shell_quote(container_graph) + " "
            + shell_quote(container_grammar) + " "
            "--out " + shell_quote(container_result);

        std::cerr << "[SVF INFO] Running Matrix solver: " << command << std::endl;
        int status = std::system(command.c_str());

        if (status != 0) {
            std::cerr << "[SVF ERROR] Matrix solver failed with status " << status << std::endl;
            return false;
        }

        std::filesystem::path result_path = abs_work_dir / "solver_result.txt";
        if (!std::filesystem::exists(result_path)) {
            std::cerr << "[SVF ERROR] Solver did not produce a result file." << std::endl;
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "[SVF ERROR] Failed to run Matrix solver: " << e.what() << std::endl;
        return false;
    }
}

bool Engine::build_index_from_result(const std::string& work_dir,
                                     const std::string& index_path) {
    if (!is_loaded_ || !svfg_) {
        std::cerr << "[SVF ERROR] No SVFG available; load a module first." << std::endl;
        return false;
    }

    StdoutToStderr redirect;

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

        std::map<SVF::NodeID, NodeMeta> node_meta;
        if (!load_node_meta(meta_csv.string(), node_meta)) {
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

        name_index_.clear();
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
            NodeMeta src_meta;
            NodeMeta dst_meta;
            if (auto it = node_meta.find(src_id); it != node_meta.end()) {
                src_meta = it->second;
            }
            if (auto it = node_meta.find(dst_id); it != node_meta.end()) {
                dst_meta = it->second;
            }

            // 2. Жесткий фильтр служебных узлов
            auto is_service_node = [](const std::string& name) {
                if (name.empty()) return true;
                if (name.rfind("node_", 0) == 0) return true;
                if (name[0] == '_') return true;
                if (name.find("(stack base object)") != std::string::npos) return true;
                if (name.find("(base object)") != std::string::npos) return true;
                if (name.find("(heap base object)") != std::string::npos) return true;
                if (name == "main_ret" || name == "dummyVal" || name == "retval") return true;
                if (name.find("call") == 0) return true;
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
            if (!src_is_field && !dst_is_field && !has_direct_assign && src_meta.name != dst_meta.name) {
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
            std::string key = "Variable: " + variable + 
                              " | Defined: " + format_location(src_meta);
            
            std::string use_loc = format_location(dst_meta);
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
                    int old_line = std::stoi(final_data[var_name].first.substr(final_data[var_name].first.rfind(':') + 1));
                    if (new_line < old_line) {
                        final_data[var_name].first = def_line;
                    }
                } catch (...) {}
                
                // Объединяем использования
                final_data[var_name].second.insert(uses.begin(), uses.end());
            }
        }

        // Формируем итоговый вектор
        name_index_.clear();
        for (const auto& [var_name, data] : final_data) {
            std::string uses_str = "";
            for (const auto& use : data.second) {
                if (!uses_str.empty()) uses_str += ", ";
                uses_str += use;
            }
            name_index_.push_back("Variable: " + var_name + 
                                  " | Defined: " + data.first + 
                                  " | Used: " + uses_str);
        }

        std::ofstream index_file_out(index_path);
        if (!index_file_out.is_open()) {
            std::cerr << "[SVF ERROR] Failed to open index file for writing: " << index_path
                      << std::endl;
            return false;
        }
        for (const std::string& entry : name_index_) {
            index_file_out << entry << '\n';
        }
        index_file_out.close();

        std::cerr << "[SVF INFO] Built CFL index with " << name_index_.size() << " entries at "
                  << index_path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[SVF ERROR] Failed to build index from result: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> Engine::lookup_name(const std::string& name) const {
    std::vector<std::string> matches;
    if (name.empty()) {
        return matches;
    }

    for (const std::string& entry : name_index_) {
        if (entry.find(name) != std::string::npos) {
            matches.push_back(entry);
        }
    }

    return matches;
}

} // namespace svf_engine
