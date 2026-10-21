#include "svf_engine/engine.hpp"
#include "Graphs/ICFGNode.h"
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "SVFIR/SVFIR.h"
#include "SVFIR/SVFStatements.h"
#include "Util/Options.h"
#include "WPA/Andersen.h"
#include <fstream>
#include <iostream>

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

bool Engine::export_graph_to_csv(const std::string& csv_output_path) {
    if (!is_loaded_) {
        std::cerr << "[SVF ERROR] No module loaded; cannot export graph." << std::endl;
        return false;
    }

    StdoutToStderr redirect;

    try {
        SVF::SVFIR* pag = SVF::SVFIR::getPAG();
        if (!pag) {
            std::cerr << "[SVF ERROR] SVFIR is not available." << std::endl;
            return false;
        }

        std::ofstream csv(csv_output_path);
        if (!csv.is_open()) {
            std::cerr << "[SVF ERROR] Failed to open CSV file for writing: " << csv_output_path
                      << std::endl;
            return false;
        }

        csv << "src_id,src_name,dst_id,dst_name,edge_kind,source_loc\n";

        const SVF::SVFStmt::PEDGEK kinds[] = {
            SVF::SVFStmt::Addr, SVF::SVFStmt::Copy, SVF::SVFStmt::Load,
            SVF::SVFStmt::Store, SVF::SVFStmt::Gep};

        for (SVF::SVFStmt::PEDGEK kind : kinds) {
            const SVF::SVFStmt::SVFStmtSetTy& edges = pag->getPTASVFStmtSet(kind);
            for (const SVF::SVFStmt* edge : edges) {
                if (!edge) {
                    continue;
                }

                SVF::NodeID src_id = edge->getSrcID();
                SVF::NodeID dst_id = edge->getDstID();

                std::string source_loc;
                if (SVF::ICFGNode* icfg_node = edge->getICFGNode()) {
                    source_loc = icfg_node->getSourceLoc();
                }

                csv << src_id << ',' << csv_escape(node_name(pag, src_id)) << ',' << dst_id
                    << ',' << csv_escape(node_name(pag, dst_id)) << ','
                    << edge_kind_name(static_cast<SVF::SVFStmt::PEDGEK>(edge->getEdgeKind()))
                    << ',' << csv_escape(source_loc) << '\n';
            }
        }

        csv.close();
        std::cerr << "[SVF INFO] Exported graph to " << csv_output_path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[SVF ERROR] Failed to export graph: " << e.what() << std::endl;
        return false;
    }
}

}
