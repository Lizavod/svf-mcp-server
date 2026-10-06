#include "svf_engine/engine.hpp"
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "Util/Options.h"
#include "WPA/Andersen.h"
#include <iostream>

namespace svf_engine {

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

}
