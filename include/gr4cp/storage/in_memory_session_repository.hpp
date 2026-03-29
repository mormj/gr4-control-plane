#pragma once

#include <mutex>
#include <unordered_map>

#include "gr4cp/domain/session.hpp"
#include "gr4cp/storage/session_repository.hpp"

namespace gr4cp::storage {

class InMemorySessionRepository final : public SessionRepository {
public:
    InMemorySessionRepository() = default;
    ~InMemorySessionRepository() override = default;

    void create(const domain::Session& session) override;
    std::optional<domain::Session> get(const std::string& id) const override;
    std::vector<domain::Session> list() const override;
    void update(const domain::Session& session) override;
    bool remove(const std::string& id) override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, domain::Session> sessions_;
};

}  // namespace gr4cp::storage
