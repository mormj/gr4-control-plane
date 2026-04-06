#include "gr4cp/app/session_service.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "gr4cp/domain/session.hpp"
#include "gr4cp/runtime/runtime_manager.hpp"
#include "gr4cp/storage/in_memory_session_repository.hpp"

namespace {

class FakeRuntimeManager final : public gr4cp::runtime::RuntimeManager {
public:
    void prepare(const gr4cp::domain::Session&) override {
        record("prepare");
    }

    void start(const gr4cp::domain::Session&) override {
        record("start");
    }

    void stop(const gr4cp::domain::Session&) override {
        record("stop");
    }

    void destroy(const gr4cp::domain::Session&) override {
        record("destroy");
    }

    void set_block_settings(const gr4cp::domain::Session&,
                            const std::string&,
                            const gr::property_map&,
                            gr4cp::runtime::BlockSettingsMode) override {
        record("set_block_settings");
    }

    gr::property_map get_block_settings(const gr4cp::domain::Session&, const std::string&) override {
        record("get_block_settings");
        return {};
    }

    void fail_on(std::string action, std::string message = "boom") {
        fail_action_ = std::move(action);
        fail_message_ = std::move(message);
    }

    std::vector<std::string> actions;

private:
    void record(const std::string& action) {
        actions.push_back(action);
        if (fail_action_ == action) {
            throw std::runtime_error(fail_message_);
        }
    }

    std::string fail_action_;
    std::string fail_message_{"boom"};
};

class SessionServiceTest : public ::testing::Test {
protected:
    gr4cp::storage::InMemorySessionRepository repository;
    FakeRuntimeManager runtime;
    gr4cp::app::SessionService service{repository, runtime};
};

TEST(DomainSessionTest, ConvertsStateToAndFromString) {
    EXPECT_EQ(gr4cp::domain::to_string(gr4cp::domain::SessionState::Stopped), "stopped");
    EXPECT_EQ(gr4cp::domain::to_string(gr4cp::domain::SessionState::Running), "running");
    EXPECT_EQ(gr4cp::domain::to_string(gr4cp::domain::SessionState::Error), "error");

    EXPECT_EQ(gr4cp::domain::session_state_from_string("stopped"), gr4cp::domain::SessionState::Stopped);
    EXPECT_EQ(gr4cp::domain::session_state_from_string("running"), gr4cp::domain::SessionState::Running);
    EXPECT_EQ(gr4cp::domain::session_state_from_string("error"), gr4cp::domain::SessionState::Error);
    EXPECT_THROW(gr4cp::domain::session_state_from_string("nope"), std::invalid_argument);
}

TEST(DomainSessionTest, FormatsTimestampAsIso8601Utc) {
    const auto timestamp = gr4cp::domain::Timestamp{std::chrono::seconds{1704067200}};
    EXPECT_EQ(gr4cp::domain::format_timestamp_utc(timestamp), "2024-01-01T00:00:00Z");
}

TEST_F(SessionServiceTest, CreateSessionSuccess) {
    const auto session = service.create("demo", "graph");

    EXPECT_TRUE(session.id.starts_with("sess_"));
    EXPECT_EQ(session.name, "demo");
    EXPECT_EQ(session.grc_content, "graph");
    EXPECT_FALSE(session.scheduler_alias.has_value());
    EXPECT_EQ(session.state, gr4cp::domain::SessionState::Stopped);
    EXPECT_FALSE(session.last_error.has_value());
    EXPECT_EQ(session.created_at, session.updated_at);

    const auto stored = repository.get(session.id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->name, "demo");
}

TEST_F(SessionServiceTest, CreateSessionStoresSelectedSchedulerAlias) {
    const auto session = service.create("demo", "graph", std::string("gr::scheduler::SimpleSingle"));

    ASSERT_TRUE(session.scheduler_alias.has_value());
    EXPECT_EQ(*session.scheduler_alias, "gr::scheduler::SimpleSingle");

    const auto stored = repository.get(session.id);
    ASSERT_TRUE(stored.has_value());
    ASSERT_TRUE(stored->scheduler_alias.has_value());
    EXPECT_EQ(*stored->scheduler_alias, "gr::scheduler::SimpleSingle");
}

TEST_F(SessionServiceTest, CreateSessionWithEmptyGrcFails) {
    EXPECT_THROW(service.create("demo", ""), gr4cp::app::ValidationError);
}

TEST_F(SessionServiceTest, GetExistingSession) {
    const auto created = service.create("demo", "graph");

    const auto fetched = service.get(created.id);

    EXPECT_EQ(fetched.id, created.id);
    EXPECT_EQ(fetched.name, "demo");
}

TEST_F(SessionServiceTest, GetMissingSessionFails) {
    EXPECT_THROW(service.get("missing"), gr4cp::app::NotFoundError);
}

TEST_F(SessionServiceTest, ListSessions) {
    const auto first = service.create("one", "graph1");
    const auto second = service.create("two", "graph2");

    const auto sessions = service.list();

    EXPECT_EQ(sessions.size(), 2U);
    EXPECT_TRUE(repository.get(first.id).has_value());
    EXPECT_TRUE(repository.get(second.id).has_value());
}

TEST_F(SessionServiceTest, StartFromStopped) {
    const auto created = service.create("demo", "graph");

    const auto started = service.start(created.id);

    EXPECT_EQ(started.state, gr4cp::domain::SessionState::Running);
    EXPECT_FALSE(started.last_error.has_value());
    EXPECT_GT(started.updated_at, created.updated_at);
    EXPECT_EQ(runtime.actions, (std::vector<std::string>{"prepare", "start"}));
}

TEST_F(SessionServiceTest, StartWhenAlreadyRunningFails) {
    const auto created = service.create("demo", "graph");
    service.start(created.id);

    EXPECT_THROW(service.start(created.id), gr4cp::app::InvalidStateError);
}

TEST_F(SessionServiceTest, StopFromRunning) {
    const auto created = service.create("demo", "graph");
    const auto started = service.start(created.id);
    runtime.actions.clear();

    const auto stopped = service.stop(created.id);

    EXPECT_EQ(stopped.state, gr4cp::domain::SessionState::Stopped);
    EXPECT_FALSE(stopped.last_error.has_value());
    EXPECT_GT(stopped.updated_at, started.updated_at);
    EXPECT_EQ(runtime.actions, (std::vector<std::string>{"stop"}));
}

TEST_F(SessionServiceTest, StopFromStoppedIsNoOpSuccess) {
    const auto created = service.create("demo", "graph");

    const auto stopped = service.stop(created.id);

    EXPECT_EQ(stopped.state, gr4cp::domain::SessionState::Stopped);
    EXPECT_EQ(stopped.updated_at, created.updated_at);
    EXPECT_TRUE(runtime.actions.empty());
}

TEST_F(SessionServiceTest, RestartFromStopped) {
    const auto created = service.create("demo", "graph");

    const auto restarted = service.restart(created.id);

    EXPECT_EQ(restarted.state, gr4cp::domain::SessionState::Running);
    EXPECT_GT(restarted.updated_at, created.updated_at);
    EXPECT_EQ(runtime.actions, (std::vector<std::string>{"destroy", "prepare", "start"}));
}

TEST_F(SessionServiceTest, RestartFromRunning) {
    const auto created = service.create("demo", "graph");
    service.start(created.id);
    runtime.actions.clear();

    const auto restarted = service.restart(created.id);

    EXPECT_EQ(restarted.state, gr4cp::domain::SessionState::Running);
    EXPECT_EQ(runtime.actions, (std::vector<std::string>{"stop", "destroy", "prepare", "start"}));
}

TEST_F(SessionServiceTest, RemoveStoppedSession) {
    const auto created = service.create("demo", "graph");

    service.remove(created.id);

    EXPECT_FALSE(repository.get(created.id).has_value());
    EXPECT_EQ(runtime.actions, (std::vector<std::string>{"destroy"}));
}

TEST_F(SessionServiceTest, RemoveRunningSession) {
    const auto created = service.create("demo", "graph");
    service.start(created.id);
    runtime.actions.clear();

    service.remove(created.id);

    EXPECT_FALSE(repository.get(created.id).has_value());
    EXPECT_EQ(runtime.actions, (std::vector<std::string>{"stop", "destroy"}));
}

TEST_F(SessionServiceTest, RuntimeFailureCausesErrorStateAndLastErrorToBeSet) {
    const auto created = service.create("demo", "graph");
    runtime.fail_on("start", "start exploded");

    EXPECT_THROW(service.start(created.id), gr4cp::app::RuntimeError);

    const auto stored = repository.get(created.id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->state, gr4cp::domain::SessionState::Error);
    ASSERT_TRUE(stored->last_error.has_value());
    EXPECT_NE(stored->last_error->find("start exploded"), std::string::npos);
}

}  // namespace
