#pragma once
#include "mcp_server.h"

namespace svf_engine {
class Engine;
}

namespace mcp_tools {
void register_svf_tools(mcp::server& server, svf_engine::Engine& engine);
}