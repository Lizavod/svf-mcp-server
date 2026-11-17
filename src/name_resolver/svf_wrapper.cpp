#include "name_resolver/svf_wrapper.hpp"

#include "name_resolver/utils.hpp"

#include "Graphs/SVFG.h"
#include "MSSA/SVFGBuilder.h"
#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "SVFIR/SVFIR.h"
#include "SVFIR/SVFValue.h"
#include "Util/SVFUtil.h"
#include "WPA/Andersen.h"

#include <iostream>
#include <stdexcept>

namespace svf_engine {

SVFWrapper::SVFWrapper() = default;

SVFWrapper::~SVFWrapper() {
    svfg_builder_.reset();
    svfg_ = nullptr;

    if (is_loaded_) {
        std::cerr << "[SVF INFO] Releasing SVF resources..." << std::endl;
        SVF::AndersenWaveDiff::releaseAndersenWaveDiff();
        SVF::SVFIR::releaseSVFIR();
        SVF::LLVMModuleSet::releaseLLVMModuleSet();
    }
}

void SVFWrapper::release_resources() {
    svfg_builder_.reset();
    svfg_ = nullptr;
    function_names_.clear();

    if (is_loaded_) {
        SVF::AndersenWaveDiff::releaseAndersenWaveDiff();
        SVF::SVFIR::releaseSVFIR();
        SVF::LLVMModuleSet::releaseLLVMModuleSet();
        is_loaded_ = false;
    }
}

bool SVFWrapper::load_module(const std::string& bc_file_path) {
    utils::StdoutToStderr redirect;

    try {
        release_resources();

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

} // namespace svf_engine
