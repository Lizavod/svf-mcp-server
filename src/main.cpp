#include "mcp_server.h"
#include "mcp_tools/tools.hpp"
#include "svf_engine/engine.hpp"
#include <iostream>

int main() {
    std::cerr << "[INFO] Initializing SVF MCP Server..." << std::endl;

    svf_engine::Engine engine;

    mcp::server::configuration srv_conf;
    mcp::server server(srv_conf);

    server.set_server_info("svf-mcp-server", "1.0.0");

    mcp::json capabilities = {{"tools", mcp::json::object()}};
    server.set_capabilities(capabilities);

    mcp_tools::register_svf_tools(server, engine);

    std::cerr << "[INFO] Server started in STDIO mode. Awaiting JSON-RPC requests..." << std::endl;
    server.start_stdio();

    return 0;
}
