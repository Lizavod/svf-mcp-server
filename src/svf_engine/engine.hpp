#pragma once
#include <memory>
#include <string>
#include <vector>

namespace SVF {
class SVFG;
class SVFGBuilder;
} // namespace SVF

namespace svf_engine {

class Engine {
  public:
    Engine();
    ~Engine();

    bool load_module(const std::string& bc_file_path);
    std::vector<std::string> get_function_names() const;
    bool export_graph_to_csv(const std::string& csv_output_path);
    bool export_cfl_graph(const std::string& graph_path, const std::string& grammar_path);
    bool export_svfg_artifacts(const std::string& output_dir);
    bool build_cfl_index(const std::string& index_path);
    std::vector<std::string> lookup_name(const std::string& name) const;

    bool is_loaded() const {
        return is_loaded_;
    }

  private:
    bool is_loaded_ = false;
    std::string current_path_;
    std::vector<std::string> function_names_;
    std::unique_ptr<SVF::SVFGBuilder> svfg_builder_;
    SVF::SVFG* svfg_ = nullptr;
    std::vector<std::string> name_index_;
};

} // namespace svf_engine
