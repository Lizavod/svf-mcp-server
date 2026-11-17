#include "name_resolver/solver_runner.hpp"

#include "name_resolver/utils.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace svf_engine {

bool run_solver(const std::string& work_dir) {
    utils::StdoutToStderr redirect;

    try {
        std::filesystem::path abs_work_dir = std::filesystem::absolute(work_dir);
        std::string host_path = abs_work_dir.string();

        std::string container_graph = "/data/graph.csv";
        std::string container_grammar = "/data/grammar.cfg"; // или grammar_cnf.cfg
        std::string container_result = "/data/solver_result.txt";

        std::string command =
            "docker run --rm --entrypoint python3 -v " +
            utils::shell_quote(host_path + ":/data") + " "
            "cfpq/py_algo:latest "
            "-m cfpq_cli.run_all_pairs_cflr "
            "NonIncrementalAllPairsCFLReachabilityMatrix " +
            utils::shell_quote(container_graph) + " " +
            utils::shell_quote(container_grammar) +
            " --out " + utils::shell_quote(container_result) +
            " 1>&2";

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

} // namespace svf_engine
