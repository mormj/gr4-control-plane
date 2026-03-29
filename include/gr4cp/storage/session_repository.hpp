#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gr4cp/domain/session.hpp"

namespace gr4cp::storage {

class SessionRepository {
public:
    virtual ~SessionRepository() = default;

    virtual void create(const domain::Session& session) = 0;
    virtual std::optional<domain::Session> get(const std::string& id) const = 0;
    virtual std::vector<domain::Session> list() const = 0;
    virtual void update(const domain::Session& session) = 0;
    virtual bool remove(const std::string& id) = 0;
};

}  // namespace gr4cp::storage
