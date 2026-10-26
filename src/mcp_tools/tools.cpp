#include "mcp_tools/tools.hpp"
#include "mcp_tool.h"
#include "svf_engine/engine.hpp"
#include <string>
#include <vector>

namespace mcp_tools {

void register_svf_tools(mcp::server& server, svf_engine::Engine& engine) {

    // Tool 1: load_module

    mcp::tool load_tool =
        mcp::tool_builder("load_module")
            .with_description("Loads a compiled .bc file, builds SVF IR, and runs Andersen's "
                              "pointer analysis. Call this first.")
            .with_string_param("file_path", "Absolute or relative path to the .bc file")
            .build();

    server.register_tool(
        load_tool,
        [&engine](const mcp::json& params, const std::string& /*session_id*/) -> mcp::json {
            std::string path = params["file_path"].get<std::string>();

            if (engine.load_module(path)) {
                std::string msg = "Successfully loaded '" + path + "'. Found " +
                                  std::to_string(engine.get_function_names().size()) +
                                  " functions.";

                return {{{"type", "text"}, {"text", msg}}};
            } else {
                throw mcp::mcp_exception(mcp::error_code::internal_error,
                                         "Failed to load SVF module.");
            }
        });

    // Tool 2: get_call_graph_info

    mcp::tool cg_tool =
        mcp::tool_builder("get_call_graph_info")
            .with_description(
                "Returns the list of functions present in the loaded module's call graph.")
            .build();

    server.register_tool(
        cg_tool,
        [&engine](const mcp::json& /*params*/, const std::string& /*session_id*/) -> mcp::json {
            if (!engine.is_loaded()) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params,
                                         "No module loaded. Please call 'load_module' first.");
            }

            std::string result = "Functions in the call graph:\n";
            for (const auto& func : engine.get_function_names()) {
                result += "- " + func + "\n";
            }

            return {{{"type", "text"}, {"text", result}}};
        });

    // Tool 3: export_graph_to_csv

    mcp::tool export_tool =
        mcp::tool_builder("export_graph_to_csv")
            .with_description(
                "Exports the current SVF graph (pointer-relevant edges) to a CSV file.")
            .with_string_param("csv_path", "Path to save the CSV file")
            .build();

    server.register_tool(
        export_tool,
        [&engine](const mcp::json& params, const std::string& /*session_id*/) -> mcp::json {
            if (!engine.is_loaded()) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params,
                                         "No module loaded. Please call 'load_module' first.");
            }

            std::string csv_path = params["csv_path"].get<std::string>();
            if (engine.export_graph_to_csv(csv_path)) {
                return {{{"type", "text"}, {"text", "Successfully exported graph to " + csv_path}}};
            } else {
                throw mcp::mcp_exception(mcp::error_code::internal_error,
                                         "Failed to export graph.");
            }
        });

    // Tool 4: export_cfl_graph

    mcp::tool export_cfl_tool =
        mcp::tool_builder("export_cfl_graph")
            .with_description("Exports the SVF graph as a CFL-reachability graph (graph.csv) and "
                              "generates the corresponding grammar (grammar.cfg).")
            .with_string_param("graph_path", "Path to save the CFL graph CSV file")
            .with_string_param("grammar_path", "Path to save the generated grammar file")
            .build();

    server.register_tool(
        export_cfl_tool,
        [&engine](const mcp::json& params, const std::string& /*session_id*/) -> mcp::json {
            if (!engine.is_loaded()) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params,
                                         "No module loaded. Please call 'load_module' first.");
            }

            std::string graph_path = params["graph_path"].get<std::string>();
            std::string grammar_path = params["grammar_path"].get<std::string>();

            if (engine.export_cfl_graph(graph_path, grammar_path)) {
                return {{{"type", "text"},
                         {"text", "Successfully exported CFL graph to " + graph_path +
                                      " and grammar to " + grammar_path}}};
            } else {
                throw mcp::mcp_exception(mcp::error_code::internal_error,
                                         "Failed to export CFL graph.");
            }
        });

    // Tool 5: build_cfl_index

    mcp::tool build_index_tool =
        mcp::tool_builder("build_cfl_index")
            .with_description("Builds a CFL-reachability index over the full SVFG using KotGLL "
                              "and saves the resulting variable definitions/uses to a CSV file.")
            .with_string_param("index_path", "Path to save the resulting index CSV file")
            .build();

    server.register_tool(
        build_index_tool,
        [&engine](const mcp::json& params, const std::string& /*session_id*/) -> mcp::json {
            if (!engine.is_loaded()) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params,
                                         "No module loaded. Please call 'load_module' first.");
            }

            std::string index_path = params["index_path"].get<std::string>();
            if (engine.build_cfl_index(index_path)) {
                return {{{"type", "text"},
                         {"text", "Successfully built CFL index and saved it to " + index_path}}};
            } else {
                throw mcp::mcp_exception(mcp::error_code::internal_error,
                                         "Failed to build CFL index.");
            }
        });

    // Tool 6: lookup_name

    mcp::tool lookup_tool =
        mcp::tool_builder("lookup_name")
            .with_description("Searches the in-memory CFL index for entries containing the given "
                              "variable name.")
            .with_string_param("name", "Variable name or substring to search for")
            .build();

    server.register_tool(
        lookup_tool,
        [&engine](const mcp::json& params, const std::string& /*session_id*/) -> mcp::json {
            if (!engine.is_loaded()) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params,
                                         "No module loaded. Please call 'load_module' first.");
            }

            std::string name = params["name"].get<std::string>();
            const std::vector<std::string> matches = engine.lookup_name(name);

            if (matches.empty()) {
                return {{{"type", "text"}, {"text", "No index entries found for '" + name + "'."}}};
            }

            std::string result = "Index entries containing '" + name + "':\n";
            for (const std::string& entry : matches) {
                result += entry + "\n";
            }

            return {{{"type", "text"}, {"text", result}}};
        });
}

} // namespace mcp_tools
