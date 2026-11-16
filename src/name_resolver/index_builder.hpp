#pragma once

#include <string>
#include <vector>

namespace svf_engine {

bool build_index_from_result(const std::string& work_dir, const std::string& index_path,
                             std::vector<std::string>& name_index);

} // namespace svf_engine
