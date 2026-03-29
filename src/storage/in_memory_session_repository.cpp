#include "gr4cp/storage/in_memory_session_repository.hpp"

#include <stdexcept>

namespace gr4cp::storage {

void InMemorySessionRepository::create(const domain::Session& session) {
    std::lock_guard lock(mutex_);
    const auto [_, inserted] = sessions_.emplace(session.id, session);
    if (!inserted) {
        throw std::runtime_error("session already exists: " + session.id);
    }
}

std::optional<domain::Session> InMemorySessionRepository::get(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(id);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<domain::Session> InMemorySessionRepository::list() const {
    std::lock_guard lock(mutex_);
    std::vector<domain::Session> sessions;
    sessions.reserve(sessions_.size());

    for (const auto& [_, session] : sessions_) {
        sessions.push_back(session);
    }

    return sessions;
}

void InMemorySessionRepository::update(const domain::Session& session) {
    std::lock_guard lock(mutex_);
    const auto it = sessions_.find(session.id);
    if (it == sessions_.end()) {
        throw std::runtime_error("unknown session: " + session.id);
    }
    it->second = session;
}

bool InMemorySessionRepository::remove(const std::string& id) {
    std::lock_guard lock(mutex_);
    return sessions_.erase(id) > 0;
}

}  // namespace gr4cp::storage
