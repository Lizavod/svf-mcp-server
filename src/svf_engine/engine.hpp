#pragma once
#include <string>
#include <vector>

namespace svf_engine {

class Engine {
  public:
    Engine();
    ~Engine();

    bool load_module(const std::string& bc_file_path);
    std::vector<std::string> get_function_names() const;

    bool is_loaded() const {
        return is_loaded_;
    }

  private:
    bool is_loaded_ = false;
    std::string current_path_;
    std::vector<std::string> function_names_;
};

} // namespace svf_engine