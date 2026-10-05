#include "mcp_tools/tools.hpp"
#include "mcp_tool.h"
#include "svf_engine/engine.hpp"
#include <string>

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
}

} // namespace mcp_tools