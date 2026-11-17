#include "name_resolver/resolver.hpp"

#include "name_resolver/artifacts.hpp"
#include "name_resolver/index_builder.hpp"
#include "name_resolver/solver_runner.hpp"
#include "name_resolver/utils.hpp"

#include <filesystem>
#include <iostream>

namespace svf_engine {

NameResolver::NameResolver() = default;

NameResolver::~NameResolver() = default;

bool NameResolver::load_module(const std::string& bc_file_path) {
    name_index_.clear();
    return svf_.load_module(bc_file_path);
}

std::vector<std::string> NameResolver::get_function_names() const {
    return svf_.get_function_names();
}

bool NameResolver::build_cfl_index(const std::string& index_path) {
    if (!svf_.is_loaded() || !svf_.svfg()) {
        std::cerr << "[SVF ERROR] No SVFG available; load a module first." << std::endl;
        return false;
    }

    utils::StdoutToStderr redirect;

    try {
        const std::filesystem::path index_file(index_path);
        if (index_file.has_parent_path()) {
            std::filesystem::create_directories(index_file.parent_path());
        }

        const std::filesystem::path work_dir(index_path + ".artifacts");
        std::filesystem::create_directories(work_dir);

        if (!prepare_artifacts(svf_.svfg(), work_dir.string())) {
            return false;
        }
        if (!run_solver(work_dir.string())) {
            return false;
        }
        if (!build_index_from_result(work_dir.string(), index_path, name_index_)) {
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "[SVF ERROR] Failed to build CFL index: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> NameResolver::lookup_name(const std::string& name) const {
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
