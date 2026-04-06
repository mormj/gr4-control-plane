#include "gr4cp/app/session_service.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

namespace gr4cp::app {

namespace {

gr4cp::domain::Timestamp now_utc() {
    return std::chrono::system_clock::now();
}

std::string generate_session_id() {
    thread_local std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned long long> distribution;

    std::ostringstream out;
    out << "sess_" << std::hex << std::setw(16) << std::setfill('0') << distribution(generator);
    return out.str();
}

std::string runtime_message(const std::string& action, const std::string& id, const std::exception& error) {
    return "runtime " + action + " failed for session " + id + ": " + error.what();
}

std::string runtime_message(const std::string& action, const std::string& id) {
    return "runtime " + action + " failed for session " + id;
}

}  // namespace

SessionService::SessionService(storage::SessionRepository& repository,
                               runtime::RuntimeManager& runtime_manager)
    : repository_(repository), runtime_manager_(runtime_manager) {}

domain::Session SessionService::create(const std::string& name, const std::string& grc, std::optional<std::string> scheduler_alias) {
    std::lock_guard lock(mutex_);

    if (grc.empty()) {
        throw ValidationError("grc_content must not be empty");
    }

    auto session = domain::Session{};
    session.name = name;
    session.grc_content = grc;
    session.scheduler_alias = std::move(scheduler_alias);
    session.state = domain::SessionState::Stopped;

    do {
        session.id = generate_session_id();
    } while (repository_.get(session.id).has_value());

    session.created_at = now_utc();
    session.updated_at = session.created_at;

    repository_.create(session);
    return session;
}

std::vector<domain::Session> SessionService::list() {
    std::lock_guard lock(mutex_);
    auto sessions = repository_.list();
    std::sort(sessions.begin(), sessions.end(), [](const domain::Session& left, const domain::Session& right) {
        if (left.created_at == right.created_at) {
            return left.id < right.id;
        }
        return left.created_at < right.created_at;
    });
    return sessions;
}

domain::Session SessionService::get(const std::string& id) {
    std::lock_guard lock(mutex_);
    return load_or_throw(id);
}

domain::Session SessionService::start(const std::string& id) {
    std::lock_guard lock(mutex_);
    auto session = load_or_throw(id);

    if (session.state == domain::SessionState::Running) {
        throw InvalidStateError("session already running: " + id);
    }

    try {
        runtime_manager_.prepare(session);
        runtime_manager_.start(session);
    } catch (const std::exception& error) {
        record_runtime_failure(std::move(session), runtime_message("start", id, error));
    } catch (...) {
        record_runtime_failure(std::move(session), runtime_message("start", id));
    }

    session.state = domain::SessionState::Running;
    session.last_error.reset();
    session.updated_at = now_utc();
    repository_.update(session);
    return session;
}

domain::Session SessionService::stop(const std::string& id) {
    std::lock_guard lock(mutex_);
    auto session = load_or_throw(id);

    if (session.state == domain::SessionState::Stopped) {
        return session;
    }

    try {
        runtime_manager_.stop(session);
    } catch (const std::exception& error) {
        record_runtime_failure(std::move(session), runtime_message("stop", id, error));
    } catch (...) {
        record_runtime_failure(std::move(session), runtime_message("stop", id));
    }

    session.state = domain::SessionState::Stopped;
    session.last_error.reset();
    session.updated_at = now_utc();
    repository_.update(session);
    return session;
}

domain::Session SessionService::restart(const std::string& id) {
    std::lock_guard lock(mutex_);
    auto session = load_or_throw(id);

    try {
        if (session.state == domain::SessionState::Running) {
            runtime_manager_.stop(session);
            session.state = domain::SessionState::Stopped;
        }
        runtime_manager_.destroy(session);
        runtime_manager_.prepare(session);
        runtime_manager_.start(session);
    } catch (const std::exception& error) {
        record_runtime_failure(std::move(session), runtime_message("restart", id, error));
    } catch (...) {
        record_runtime_failure(std::move(session), runtime_message("restart", id));
    }

    session.state = domain::SessionState::Running;
    session.last_error.reset();
    session.updated_at = now_utc();
    repository_.update(session);
    return session;
}

void SessionService::remove(const std::string& id) {
    std::lock_guard lock(mutex_);
    auto session = load_or_throw(id);

    try {
        if (session.state == domain::SessionState::Running) {
            runtime_manager_.stop(session);
            session.state = domain::SessionState::Stopped;
        }
        runtime_manager_.destroy(session);
    } catch (const std::exception& error) {
        record_runtime_failure(std::move(session), runtime_message("remove", id, error));
    } catch (...) {
        record_runtime_failure(std::move(session), runtime_message("remove", id));
    }

    if (!repository_.remove(id)) {
        throw NotFoundError("session not found: " + id);
    }
}

domain::Session SessionService::load_or_throw(const std::string& id) {
    auto session = repository_.get(id);
    if (!session.has_value()) {
        throw NotFoundError("session not found: " + id);
    }
    return *session;
}

[[noreturn]] void SessionService::record_runtime_failure(domain::Session session, const std::string& message) {
    session.state = domain::SessionState::Error;
    session.last_error = message;
    session.updated_at = now_utc();
    repository_.update(session);
    throw RuntimeError(message);
}

}  // namespace gr4cp::app
