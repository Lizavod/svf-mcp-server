#include "mcp_tools/tools.hpp"
#include "mcp_tool.h"
#include "name_resolver/resolver.hpp"
#include <string>
#include <vector>

namespace mcp_tools {

void register_svf_tools(mcp::server& server, svf_engine::NameResolver& resolver) {

    // Tool 1: load_module

    mcp::tool load_tool =
        mcp::tool_builder("load_module")
            .with_description("Loads a compiled .bc file, builds SVF IR, and runs Andersen's "
                              "pointer analysis. Call this first.")
            .with_string_param("file_path", "Absolute or relative path to the .bc file")
            .build();

    server.register_tool(
        load_tool,
        [&resolver](const mcp::json& params, const std::string& /*session_id*/) -> mcp::json {
            std::string path = params["file_path"].get<std::string>();

            if (resolver.load_module(path)) {
                std::string msg = "Successfully loaded '" + path + "'. Found " +
                                  std::to_string(resolver.get_function_names().size()) +
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
        [&resolver](const mcp::json& /*params*/, const std::string& /*session_id*/) -> mcp::json {
            if (!resolver.is_loaded()) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params,
                                         "No module loaded. Please call 'load_module' first.");
            }

            std::string result = "Functions in the call graph:\n";
            for (const auto& func : resolver.get_function_names()) {
                result += "- " + func + "\n";
            }

            return {{{"type", "text"}, {"text", result}}};
        });

    // Tool 3: build_cfl_index

    mcp::tool build_index_tool =
        mcp::tool_builder("build_cfl_index")
            .with_description("Builds a CFL-reachability index over the full SVFG using the external solver "
                              "and saves the resulting variable definitions/uses to a CSV file.")
            .with_string_param("index_path", "Path to save the resulting index CSV file")
            .build();

    server.register_tool(
        build_index_tool,
        [&resolver](const mcp::json& params, const std::string& /*session_id*/) -> mcp::json {
            if (!resolver.is_loaded()) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params,
                                         "No module loaded. Please call 'load_module' first.");
            }

            std::string index_path = params["index_path"].get<std::string>();
            if (resolver.build_cfl_index(index_path)) {
                return {{{"type", "text"},
                         {"text", "Successfully built CFL index and saved it to " + index_path}}};
            } else {
                throw mcp::mcp_exception(mcp::error_code::internal_error,
                                         "Failed to build CFL index.");
            }
        });

    // Tool 4: lookup_name

    mcp::tool lookup_tool =
        mcp::tool_builder("lookup_name")
            .with_description("Searches the in-memory CFL index for entries containing the given "
                              "variable name.")
            .with_string_param("name", "Variable name or substring to search for")
            .build();

    server.register_tool(
        lookup_tool,
        [&resolver](const mcp::json& params, const std::string& /*session_id*/) -> mcp::json {
            if (!resolver.is_loaded()) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params,
                                         "No module loaded. Please call 'load_module' first.");
            }

            std::string name = params["name"].get<std::string>();
            const std::vector<std::string> matches = resolver.lookup_name(name);

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
