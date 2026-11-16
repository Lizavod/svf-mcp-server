#pragma once

#include "name_resolver/svf_wrapper.hpp"

#include <string>
#include <vector>

namespace svf_engine {

class NameResolver {
  public:
    NameResolver();
    ~NameResolver();

    bool load_module(const std::string& bc_file_path);
    std::vector<std::string> get_function_names() const;
    bool build_cfl_index(const std::string& index_path);
    std::vector<std::string> lookup_name(const std::string& name) const;

    bool is_loaded() const {
        return svf_.is_loaded();
    }

  private:
    SVFWrapper svf_;
    std::vector<std::string> name_index_;
};

} // namespace svf_engine
