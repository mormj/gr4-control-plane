#pragma once

#include "gr4cp/runtime/runtime_manager.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gr4cp::runtime {

class Gr4RuntimeManager final : public RuntimeManager {
public:
    explicit Gr4RuntimeManager(std::vector<std::filesystem::path> plugin_directories = {});
    ~Gr4RuntimeManager() override;

    void prepare(const domain::Session& session) override;
    void start(const domain::Session& session) override;
    void stop(const domain::Session& session) override;
    void destroy(const domain::Session& session) override;
    void set_block_settings(const domain::Session& session,
                            const std::string& block_unique_name,
                            const gr::property_map& patch,
                            BlockSettingsMode mode) override;
    gr::property_map get_block_settings(const domain::Session& session, const std::string& block_unique_name) override;

private:
    struct Execution;

    static std::vector<std::filesystem::path> default_plugin_directories();

    Execution& prepare_locked(const domain::Session& session);
    Execution* find_execution_locked(const std::string& session_id);
    void stop_locked(const domain::Session& session, Execution& execution);
    void destroy_locked(const domain::Session& session);

    std::vector<std::filesystem::path> plugin_directories_;
    std::unordered_map<std::string, std::unique_ptr<Execution>> executions_;
    std::mutex mutex_;
};

}  // namespace gr4cp::runtime
