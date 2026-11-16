#pragma once

#include <memory>
#include <string>
#include <vector>

namespace SVF {
class SVFG;
class SVFGBuilder;
} // namespace SVF

namespace svf_engine {

class SVFWrapper {
  public:
    SVFWrapper();
    ~SVFWrapper();

    SVFWrapper(const SVFWrapper&) = delete;
    SVFWrapper& operator=(const SVFWrapper&) = delete;

    bool load_module(const std::string& bc_file_path);
    const std::vector<std::string>& get_function_names() const {
        return function_names_;
    }

    bool is_loaded() const {
        return is_loaded_;
    }

    const std::string& current_path() const {
        return current_path_;
    }

    SVF::SVFG* svfg() const {
        return svfg_;
    }

  private:
    void release_resources();

    bool is_loaded_ = false;
    std::string current_path_;
    std::vector<std::string> function_names_;
    std::unique_ptr<SVF::SVFGBuilder> svfg_builder_;
    SVF::SVFG* svfg_ = nullptr;
};

} // namespace svf_engine
