#pragma once

#include "Graphs/SVFG.h"
#include "SVFIR/SVFStatements.h"
#include "SVFIR/SVFValue.h"

#include <map>
#include <string>
#include <vector>

namespace svf_engine {
namespace utils {

struct NodeMeta {
    std::string name;
    std::string file;
    std::string line;
};

class StdoutToStderr {
  public:
    StdoutToStderr();
    ~StdoutToStderr();

    StdoutToStderr(const StdoutToStderr&) = delete;
    StdoutToStderr& operator=(const StdoutToStderr&) = delete;

  private:
    int saved_stdout_;
};

std::string csv_escape(const std::string& field);
std::string edge_kind_name(SVF::SVFStmt::PEDGEK kind);
std::string clean_name(const std::string& raw);
std::string node_name(const SVF::SVFIR* pag, SVF::NodeID id);
bool json_field(const std::string& raw, const std::string& key, std::string& value);
std::string compact_source_loc(const std::string& raw);
void split_source_loc(const std::string& raw, std::string& file, std::string& line);
std::string format_location(const NodeMeta& meta);
std::string svfg_node_name(const SVF::SVFG* svfg, const SVF::SVFGNode* node);
std::string vfg_edge_label(SVF::VFGEdge::VFGEdgeK kind);
std::vector<std::string> split_csv_line(const std::string& line);
bool parse_node_id(const std::string& value, SVF::NodeID& id);
bool load_node_meta(const std::string& meta_path, std::map<SVF::NodeID, NodeMeta>& node_meta);
std::string shell_quote(const std::string& value);

} // namespace utils
} // namespace svf_engine
