#pragma once

#include "gr4cp/runtime/runtime_manager.hpp"

#include <unordered_map>

namespace gr4cp::runtime {

class StubRuntimeManager final : public RuntimeManager {
public:
    StubRuntimeManager() = default;
    ~StubRuntimeManager() override = default;

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
    std::unordered_map<std::string, std::unordered_map<std::string, gr::property_map>> settings_;
};

}  // namespace gr4cp::runtime
