#pragma once

#include <string>

namespace SVF {
class SVFG;
} // namespace SVF

namespace svf_engine {

bool prepare_artifacts(const SVF::SVFG* svfg, const std::string& output_dir);

} // namespace svf_engine
