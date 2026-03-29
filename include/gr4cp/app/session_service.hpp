#pragma once

#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "gr4cp/domain/session.hpp"
#include "gr4cp/runtime/runtime_manager.hpp"
#include "gr4cp/storage/session_repository.hpp"

namespace gr4cp::app {

class NotFoundError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ValidationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class InvalidStateError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class RuntimeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class SessionService {
public:
    SessionService(storage::SessionRepository& repository, runtime::RuntimeManager& runtime_manager);

    domain::Session create(const std::string& name, const std::string& grc);
    std::vector<domain::Session> list();
    domain::Session get(const std::string& id);
    domain::Session start(const std::string& id);
    domain::Session stop(const std::string& id);
    domain::Session restart(const std::string& id);
    void remove(const std::string& id);

private:
    domain::Session load_or_throw(const std::string& id);
    [[noreturn]] void record_runtime_failure(domain::Session session, const std::string& message);

    storage::SessionRepository& repository_;
    runtime::RuntimeManager& runtime_manager_;
    mutable std::mutex mutex_;
};

}  // namespace gr4cp::app
