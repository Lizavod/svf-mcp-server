#include "name_resolver/utils.hpp"

#include "Graphs/ICFGNode.h"
#include "Graphs/SVFG.h"
#include "SVFIR/SVFIR.h"
#include "SVFIR/SVFStatements.h"
#include "SVFIR/SVFValue.h"
#include "WPA/Andersen.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

namespace svf_engine {
namespace utils {

StdoutToStderr::StdoutToStderr() {
    saved_stdout_ = dup(STDOUT_FILENO);
    dup2(STDERR_FILENO, STDOUT_FILENO);
}

StdoutToStderr::~StdoutToStderr() {
    fflush(stdout);
    dup2(saved_stdout_, STDOUT_FILENO);
    close(saved_stdout_);
}

std::string csv_escape(const std::string& field) {
    if (field.find_first_of(",\"\n\r") == std::string::npos) {
        return field;
    }

    std::string escaped = "\"";
    for (char c : field) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += '"';
    return escaped;
}

std::string edge_kind_name(SVF::SVFStmt::PEDGEK kind) {
    switch (kind) {
        case SVF::SVFStmt::Addr:
            return "Addr";
        case SVF::SVFStmt::Copy:
            return "Copy";
        case SVF::SVFStmt::Store:
            return "Store";
        case SVF::SVFStmt::Load:
            return "Load";
        case SVF::SVFStmt::Call:
            return "Call";
        case SVF::SVFStmt::Ret:
            return "Ret";
        case SVF::SVFStmt::Gep:
            return "Gep";
        case SVF::SVFStmt::Phi:
            return "Phi";
        case SVF::SVFStmt::Select:
            return "Select";
        case SVF::SVFStmt::Cmp:
            return "Cmp";
        case SVF::SVFStmt::BinaryOp:
            return "BinaryOp";
        case SVF::SVFStmt::UnaryOp:
            return "UnaryOp";
        case SVF::SVFStmt::Branch:
            return "Branch";
        case SVF::SVFStmt::ThreadFork:
            return "ThreadFork";
        case SVF::SVFStmt::ThreadJoin:
            return "ThreadJoin";
    }

    return "Unknown";
}

std::string clean_name(const std::string& raw) {
    size_t begin = 0;
    while (begin < raw.size() && std::isspace(static_cast<unsigned char>(raw[begin]))) {
        ++begin;
    }
    const std::string name = raw.substr(begin);

    if (name.empty() || name.find("dummy") != std::string::npos || name.find("node_") == 0 ||
        name.find("call") == 0 || name.find("(stack") == 0 || name.find("(heap") == 0 ||
        name.find("retval") != std::string::npos) {
        return "";
    }

    const size_t dot_pos = name.find('.');
    if (dot_pos != std::string::npos) {
        return name.substr(0, dot_pos);
    }
    return name;
}

std::string node_name(const SVF::SVFIR* pag, SVF::NodeID id) {
    const SVF::SVFVar* var = pag->getSVFVar(id);
    if (var) {
        std::string name = var->getValueName();
        if (!name.empty()) {
            return name;
        }
    }

    return "node_" + std::to_string(id);
}

bool json_field(const std::string& raw, const std::string& key, std::string& value) {
    const std::string pattern = "\"" + key + "\"";
    size_t pos = raw.find(pattern);
    if (pos == std::string::npos) {
        return false;
    }

    pos = raw.find(':', pos + pattern.size());
    if (pos == std::string::npos) {
        return false;
    }
    ++pos;

    while (pos < raw.size() && std::isspace(static_cast<unsigned char>(raw[pos]))) {
        ++pos;
    }
    if (pos >= raw.size()) {
        return false;
    }

    if (raw[pos] == '"') {
        ++pos;
        std::string result;
        while (pos < raw.size() && raw[pos] != '"') {
            if (raw[pos] == '\\' && pos + 1 < raw.size()) {
                result += raw[pos + 1];
                pos += 2;
            } else {
                result += raw[pos++];
            }
        }
        value = result;
    } else {
        const size_t start = pos;
        while (pos < raw.size() &&
               (std::isdigit(static_cast<unsigned char>(raw[pos])) || raw[pos] == '-')) {
            ++pos;
        }
        value = raw.substr(start, pos - start);
    }

    return !value.empty();
}

std::string compact_source_loc(const std::string& raw) {
    std::string file;
    std::string line;

    const bool has_file = json_field(raw, "fl", file) || json_field(raw, "file", file);
    const bool has_line = json_field(raw, "ln", line);

    if (has_file && has_line) {
        return file + ":" + line;
    }
    if (has_file) {
        return file;
    }
    if (has_line) {
        return line;
    }

    return raw;
}

void split_source_loc(const std::string& raw, std::string& file, std::string& line) {
    file.clear();
    line.clear();

    if (!json_field(raw, "fl", file)) {
        json_field(raw, "file", file);
    }
    if (!json_field(raw, "ln", line)) {
        json_field(raw, "line", line);
    }

    if (file.empty() && line.empty()) {
        const size_t colon = raw.rfind(':');
        if (colon != std::string::npos && colon + 1 < raw.size()) {
            bool digits = true;
            for (size_t i = colon + 1; i < raw.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(raw[i]))) {
                    digits = false;
                    break;
                }
            }
            if (digits) {
                file = raw.substr(0, colon);
                line = raw.substr(colon + 1);
            }
        }
    }
}

std::string format_location(const NodeMeta& meta) {
    if (!meta.file.empty() && !meta.line.empty()) {
        return meta.file + ":" + meta.line;
    }
    if (!meta.file.empty()) {
        return meta.file;
    }
    if (!meta.line.empty()) {
        return meta.line;
    }
    return "unknown";
}

std::string svfg_node_name(const SVF::SVFG* svfg, const SVF::SVFGNode* node) {
    if (!node) {
        return "node_unknown";
    }

    if (const SVF::SVFVar* var = node->getValue()) {
        const std::string value_name = var->getValueName();
        if (!value_name.empty()) {
            return value_name;
        }
    }

    const SVF::SVFIR* pag = (svfg && svfg->getPTA()) ? svfg->getPTA()->getPAG() : nullptr;
    if (pag) {
        const SVF::NodeBS defs = node->getDefSVFVars();
        for (SVF::NodeID id : defs) {
            const SVF::SVFVar* var = pag->getSVFVar(id);
            if (var && !var->getValueName().empty()) {
                return var->getValueName();
            }
        }
    }

    if (const SVF::ICFGNode* icfg = node->getICFGNode()) {
        for (const SVF::SVFStmt* stmt : icfg->getSVFStmts()) {
            if (!stmt) {
                continue;
            }
            const SVF::SVFVar* var = stmt->getDstNode();
            if (var && !var->getValueName().empty()) {
                return var->getValueName();
            }
            var = stmt->getSrcNode();
            if (var && !var->getValueName().empty()) {
                return var->getValueName();
            }
        }
    }

    return "node_" + std::to_string(node->getId());
}

std::string vfg_edge_label(SVF::VFGEdge::VFGEdgeK kind) {
    switch (kind) {
        case SVF::VFGEdge::IntraDirectVF:
            return "assign";
        case SVF::VFGEdge::IntraIndirectVF:
            return "mem";
        case SVF::VFGEdge::CallDirVF:
        case SVF::VFGEdge::CallIndVF:
            return "call";
        case SVF::VFGEdge::RetDirVF:
        case SVF::VFGEdge::RetIndVF:
            return "ret";
        case SVF::VFGEdge::TheadMHPIndirectVF:
            return "mem";
    }
    return "mem";
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(field);
            field.clear();
        } else if (c != '\r') {
            field += c;
        }
    }
    fields.push_back(field);

    return fields;
}

bool parse_node_id(const std::string& value, SVF::NodeID& id) {
    if (value.empty()) {
        return false;
    }
    for (char c : value) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    try {
        id = static_cast<SVF::NodeID>(std::stoull(value));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool load_node_meta(const std::string& meta_path, std::map<SVF::NodeID, NodeMeta>& node_meta) {
    std::ifstream meta_file(meta_path);
    if (!meta_file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(meta_file, line)) {
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = split_csv_line(line);
        if (fields.size() < 4) {
            continue;
        }

        SVF::NodeID id = 0;
        if (!parse_node_id(fields[0], id)) {
            continue;
        }

        node_meta[id] = NodeMeta{fields[1], fields[2], fields[3]};
    }

    return true;
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

} // namespace utils
} // namespace svf_engine
