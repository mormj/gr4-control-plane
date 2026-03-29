#pragma once

#include "gr4cp/domain/session.hpp"
#include "gr4cp/runtime/block_settings.hpp"

namespace gr4cp::runtime {

class RuntimeManager {
public:
    virtual ~RuntimeManager() = default;

    virtual void prepare(const domain::Session& session) = 0;
    virtual void start(const domain::Session& session) = 0;
    virtual void stop(const domain::Session& session) = 0;
    virtual void destroy(const domain::Session& session) = 0;
    virtual void set_block_settings(const domain::Session& session,
                                    const std::string& block_unique_name,
                                    const gr::property_map& patch,
                                    BlockSettingsMode mode) = 0;
    virtual gr::property_map get_block_settings(const domain::Session& session,
                                                const std::string& block_unique_name) = 0;
};

}  // namespace gr4cp::runtime
