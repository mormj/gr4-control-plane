#include "gr4cp/runtime/stub_runtime_manager.hpp"

#include <iostream>

namespace gr4cp::runtime {

namespace {

void log_action(const char* action, const domain::Session& session) {
    std::clog << "[stub-runtime] " << action << ' ' << session.id << '\n';
}

}  // namespace

void StubRuntimeManager::prepare(const domain::Session& session) {
    log_action("prepare", session);
}

void StubRuntimeManager::start(const domain::Session& session) {
    log_action("start", session);
}

void StubRuntimeManager::stop(const domain::Session& session) {
    log_action("stop", session);
}

void StubRuntimeManager::destroy(const domain::Session& session) {
    log_action("destroy", session);
    settings_.erase(session.id);
}

void StubRuntimeManager::set_block_settings(const domain::Session& session,
                                            const std::string& block_unique_name,
                                            const gr::property_map& patch,
                                            BlockSettingsMode) {
    log_action("set_block_settings", session);
    auto& block_settings = settings_[session.id][block_unique_name];
    for (const auto& [key, value] : patch) {
        block_settings.insert_or_assign(key, value);
    }
}

gr::property_map StubRuntimeManager::get_block_settings(const domain::Session& session,
                                                        const std::string& block_unique_name) {
    log_action("get_block_settings", session);
    const auto session_it = settings_.find(session.id);
    if (session_it == settings_.end()) {
        return {};
    }
    const auto block_it = session_it->second.find(block_unique_name);
    if (block_it == session_it->second.end()) {
        return {};
    }
    return block_it->second;
}

}  // namespace gr4cp::runtime
