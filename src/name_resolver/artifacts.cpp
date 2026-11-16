#include "name_resolver/artifacts.hpp"

#include "name_resolver/utils.hpp"

#include "Graphs/ICFGNode.h"
#include "Graphs/SVFG.h"
#include "SVFIR/SVFIR.h"
#include "SVFIR/SVFStatements.h"
#include "SVFIR/SVFValue.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace svf_engine {

bool prepare_artifacts(const SVF::SVFG* svfg, const std::string& output_dir) {
    if (!svfg) {
        std::cerr << "[SVF ERROR] No SVFG available; load a module first." << std::endl;
        return false;
    }

    utils::StdoutToStderr redirect;

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

        std::map<SVF::NodeID, utils::NodeMeta> node_meta;
        auto collect_meta = [&](SVF::NodeID id) {
            if (node_meta.find(id) != node_meta.end()) {
                return;
            }

            utils::NodeMeta meta;
            const SVF::SVFGNode* node = svfg->hasSVFGNode(id) ? svfg->getSVFGNode(id) : nullptr;
            meta.name = node ? utils::svfg_node_name(svfg, node) : ("node_" + std::to_string(id));

            if (node) {
                if (const SVF::ICFGNode* icfg = node->getICFGNode()) {
                    utils::split_source_loc(icfg->getSourceLoc(), meta.file, meta.line);
                    if (meta.file.empty() && meta.line.empty() && icfg->getFun()) {
                        utils::split_source_loc(icfg->getFun()->getSourceLoc(), meta.file,
                                                meta.line);
                    }
                } else if (node->getFun()) {
                    utils::split_source_loc(node->getFun()->getSourceLoc(), meta.file, meta.line);
                }
            }

            node_meta[id] = meta;
            meta_file << id << ',' << utils::csv_escape(meta.name) << ','
                      << utils::csv_escape(meta.file) << ',' << utils::csv_escape(meta.line) << '\n';
        };

        for (auto it = svfg->begin(); it != svfg->end(); ++it) {
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
                        const std::string fallback = utils::vfg_edge_label(edge_kind);
                        if (fallback == "mem") {
                            uses_unindexed_mem = true;
                        }
                        labels.push_back(fallback);
                    }
                } else {
                    labels.push_back(utils::vfg_edge_label(edge_kind));
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

} // namespace svf_engine
