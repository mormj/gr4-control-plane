#include "gr4cp/domain/session.hpp"
#include "gr4cp/runtime/gr4_runtime_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <format>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

gr4cp::domain::Session make_session(std::string id, std::string yaml) {
    gr4cp::domain::Session session;
    session.id = std::move(id);
    session.name = session.id;
    session.grc_content = std::move(yaml);
    session.state = gr4cp::domain::SessionState::Stopped;
    session.created_at = std::chrono::system_clock::now();
    session.updated_at = session.created_at;
    return session;
}

std::string continuous_graph_yaml() {
    return std::format(R"(blocks:
  - id: "gr::basic::SignalGenerator<float32>"
    parameters:
      name: "src0"
      sample_rate: 1000.0
      chunk_size: 32
      signal_type: "Sin"
      frequency: 25.0
      amplitude: 1.0
      offset: 0.0
      phase: 0.0
  - id: "gr::testing::NullSink<float32>"
    parameters:
      name: "sink0"
connections:
  - ["src0", 0, "sink0", 0]
)");
}

std::string legacy_minimal_graph_json() {
    return R"({
  "graph_name": "legacy_null_source_to_sink",
  "source_format": "grc",
  "blocks": [
    {
      "instance_name": "src0",
      "block_type": "gr::testing::NullSource<float32>",
      "enabled": true,
      "raw_parameters": {}
    },
    {
      "instance_name": "sink0",
      "block_type": "gr::testing::NullSink<float32>",
      "enabled": true,
      "raw_parameters": {}
    }
  ],
  "connections": [
    {
      "source_block": "src0",
      "source_port_token": "out",
      "dest_block": "sink0",
      "dest_port_token": "in"
    }
  ]
})";
}

std::string studio_inline_graph_yaml() {
    return R"(# gr4-studio inline grc
metadata:
  name: Untitled Graph
  description: ""
blocks:
  - id: "gr::testing::NullSink<float32>"
    parameters:
      name: gr__testing__NullSink_float32__5
  - id: "gr::testing::NullSource<float32>"
    parameters:
      name: gr__testing__NullSource_float32__2
connections:
  - [gr__testing__NullSource_float32__2, out, gr__testing__NullSink_float32__5, in]
)";
}

class Gr4RuntimeManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        try {
            probe_session_ = make_session("probe", continuous_graph_yaml());
            runtime_.prepare(probe_session_);
            runtime_.destroy(probe_session_);
        } catch (const std::exception& error) {
            GTEST_SKIP() << "GNU Radio 4 runtime unavailable: " << error.what();
        }
    }

    gr4cp::runtime::Gr4RuntimeManager runtime_;
    gr4cp::domain::Session probe_session_;
};

TEST_F(Gr4RuntimeManagerTest, PrepareStartStopAndDestroyManageExecutableGraphLifecycle) {
    auto session = make_session("runtime_lifecycle", continuous_graph_yaml());

    runtime_.prepare(session);
    runtime_.start(session);
    std::this_thread::sleep_for(250ms);

    runtime_.stop(session);
    runtime_.destroy(session);
    SUCCEED();
}

TEST_F(Gr4RuntimeManagerTest, PrepareAfterStopReusesPreparedExecutionForRestart) {
    auto session = make_session("runtime_restart", continuous_graph_yaml());

    runtime_.prepare(session);
    runtime_.start(session);
    std::this_thread::sleep_for(250ms);

    runtime_.stop(session);
    runtime_.prepare(session);
    runtime_.start(session);
    std::this_thread::sleep_for(250ms);

    runtime_.destroy(session);
}

TEST_F(Gr4RuntimeManagerTest, InvalidGraphProducesActionablePrepareError) {
    auto session = make_session("runtime_invalid_graph", R"(blocks:
  - id: "gr::does::not::Exist"
    parameters:
      name: "bad0"
connections: []
)");

    EXPECT_THROW(
        {
            try {
                runtime_.prepare(session);
            } catch (const std::runtime_error& error) {
                EXPECT_NE(std::string(error.what()).find("prepare"), std::string::npos);
                throw;
            }
        },
        std::runtime_error);
}

TEST_F(Gr4RuntimeManagerTest, LegacyMinimalGraphShapeIsNormalizedBeforePrepare) {
    auto session = make_session("runtime_legacy_graph", legacy_minimal_graph_json());

    EXPECT_NO_THROW(runtime_.prepare(session));
    EXPECT_NO_THROW(runtime_.start(session));
    std::this_thread::sleep_for(100ms);
    EXPECT_NO_THROW(runtime_.stop(session));
    EXPECT_NO_THROW(runtime_.destroy(session));
}

TEST_F(Gr4RuntimeManagerTest, StudioInlineGraphShapeIsNormalizedBeforePrepare) {
    auto session = make_session("runtime_studio_graph", studio_inline_graph_yaml());

    EXPECT_NO_THROW(runtime_.prepare(session));
    EXPECT_NO_THROW(runtime_.start(session));
    std::this_thread::sleep_for(100ms);
    EXPECT_NO_THROW(runtime_.stop(session));
    EXPECT_NO_THROW(runtime_.destroy(session));
}

TEST_F(Gr4RuntimeManagerTest, RunningSessionSupportsBlockSettingsMessageRoundTrip) {
    auto session = make_session("runtime_block_settings", continuous_graph_yaml());

    runtime_.prepare(session);
    runtime_.start(session);
    std::this_thread::sleep_for(100ms);

    EXPECT_NO_THROW(runtime_.set_block_settings(
        session,
        "src0",
        gr::property_map{{"frequency", 1250.0}, {"amplitude", 0.5}},
        gr4cp::runtime::BlockSettingsMode::Staged));

    const auto settings = runtime_.get_block_settings(session, "src0");
    ASSERT_TRUE(settings.contains("frequency"));
    ASSERT_TRUE(settings.contains("amplitude"));
    EXPECT_EQ(settings.at("frequency").value_or(0.0), 1250.0);
    EXPECT_EQ(settings.at("amplitude").value_or(0.0), 0.5);

    runtime_.stop(session);
    runtime_.destroy(session);
}

TEST_F(Gr4RuntimeManagerTest, RunningSessionResolvesStudioBlockNameForSettingsRoundTrip) {
    auto session = make_session("runtime_studio_name_settings", R"(blocks:
  - id: "gr::basic::SignalGenerator<float32>"
    parameters:
      name: "gr__basic__SignalGenerator_float32__1"
      sample_rate: 1000.0
      chunk_size: 32
      signal_type: "Sin"
      frequency: 25.0
      amplitude: 1.0
      offset: 0.0
      phase: 0.0
  - id: "gr::testing::NullSink<float32>"
    parameters:
      name: "gr__testing__NullSink_float32__1"
connections:
  - ["gr__basic__SignalGenerator_float32__1", 0, "gr__testing__NullSink_float32__1", 0]
)");

    runtime_.prepare(session);
    runtime_.start(session);
    std::this_thread::sleep_for(100ms);

    EXPECT_NO_THROW(runtime_.set_block_settings(
        session,
        "gr__basic__SignalGenerator_float32__1",
        gr::property_map{{"frequency", 1250.0}, {"amplitude", 0.5}},
        gr4cp::runtime::BlockSettingsMode::Staged));

    const auto settings = runtime_.get_block_settings(session, "gr__basic__SignalGenerator_float32__1");
    ASSERT_TRUE(settings.contains("frequency"));
    ASSERT_TRUE(settings.contains("amplitude"));
    EXPECT_EQ(settings.at("frequency").value_or(0.0), 1250.0);
    EXPECT_EQ(settings.at("amplitude").value_or(0.0), 0.5);

    runtime_.stop(session);
    runtime_.destroy(session);
}

TEST_F(Gr4RuntimeManagerTest, RunningSessionAlsoResolvesInternalRuntimeIdentifierForSettingsRoundTrip) {
    auto session = make_session("runtime_internal_id_settings", continuous_graph_yaml());

    runtime_.prepare(session);
    runtime_.start(session);
    std::this_thread::sleep_for(100ms);

    EXPECT_NO_THROW(runtime_.set_block_settings(
        session,
        "gr::basic::SignalGenerator<float32>#0",
        gr::property_map{{"frequency", 900.0}},
        gr4cp::runtime::BlockSettingsMode::Staged));

    const auto settings = runtime_.get_block_settings(session, "gr::basic::SignalGenerator<float32>#0");
    ASSERT_TRUE(settings.contains("frequency"));
    EXPECT_EQ(settings.at("frequency").value_or(0.0), 900.0);

    runtime_.stop(session);
    runtime_.destroy(session);
}

TEST_F(Gr4RuntimeManagerTest, MissingBlockSettingsLookupIncludesAvailableRuntimeBlockNames) {
    auto session = make_session("runtime_block_lookup_debug", continuous_graph_yaml());

    runtime_.prepare(session);
    runtime_.start(session);
    std::this_thread::sleep_for(100ms);

    try {
        (void)runtime_.get_block_settings(session, "missing_block");
        FAIL() << "expected BlockNotFoundError";
    } catch (const gr4cp::runtime::BlockNotFoundError& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("missing_block"), std::string::npos);
        EXPECT_NE(message.find("available runtime blocks"), std::string::npos);
        EXPECT_NE(message.find("src0"), std::string::npos);
        EXPECT_NE(message.find("sink0"), std::string::npos);
    }

    runtime_.stop(session);
    runtime_.destroy(session);
}

}  // namespace
