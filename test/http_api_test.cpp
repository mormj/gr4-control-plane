#include "gr4cp/api/http_server.hpp"

#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "gr4cp/app/block_catalog_service.hpp"
#include "gr4cp/app/block_settings_service.hpp"
#include "gr4cp/app/session_service.hpp"
#include "gr4cp/catalog/block_catalog_provider.hpp"
#include "gr4cp/runtime/runtime_manager.hpp"
#include "gr4cp/storage/in_memory_session_repository.hpp"

namespace {

class TestBlockCatalogProvider final : public gr4cp::catalog::BlockCatalogProvider {
public:
    std::vector<gr4cp::domain::BlockDescriptor> list() const override {
        return {
            {
                .id = "gr::blocks::math::Add<float32, std::plus<float32>>",
                .name = "Add<float32>",
                .category = "Math",
                .summary = "Adds two float streams",
                .inputs = {[] {
                    gr4cp::domain::BlockPortDescriptor port;
                    port.name = "in";
                    port.type = "float32";
                    port.cardinality_kind = gr4cp::domain::BlockPortCardinalityKind::Dynamic;
                    port.current_port_count = 2;
                    port.render_port_count = 2;
                    port.min_port_count = 1;
                    port.max_port_count = 32;
                    port.size_parameter = "n_inputs";
                    port.handle_name_template = "in#${index}";
                    return port;
                }()},
                .outputs = {{"out", "float32"}},
                .parameters = {[] {
                    gr4cp::domain::BlockParameterDescriptor parameter("scale", "float", false, 1.0, "Scale factor");
                    parameter.runtime_mutability = "mutable";
                    parameter.value_kind = "scalar";
                    parameter.ui_hint = "advanced";
                    return parameter;
                }()},
            },
            {
                .id = "blocks.math.add_dynamic",
                .name = "Add",
                .category = "Math",
                .summary = "Adds a dynamic number of float streams",
                .inputs = {[] {
                    gr4cp::domain::BlockPortDescriptor port;
                    port.name = "in";
                    port.type = "float";
                    port.cardinality_kind = gr4cp::domain::BlockPortCardinalityKind::Dynamic;
                    port.current_port_count = 3;
                    port.render_port_count = 3;
                    port.min_port_count = 1;
                    port.max_port_count = 32;
                    port.size_parameter = "n_inputs";
                    port.handle_name_template = "in#${index}";
                    return port;
                }()},
                .outputs = {{"out", "float"}},
                .parameters = {{"n_inputs", "int", false, 3, "Number of inputs"}},
            },
            {
                .id = "blocks.sources.signal_source_f",
                .name = "Signal Source",
                .category = "Sources",
                .summary = "",
                .inputs = {},
                .outputs = {{"out", "float"}},
                .parameters = {{"frequency", "float", false, 1000.0, ""}},
            },
            {
                .id = "blocks.math.add_ff",
                .name = "Add",
                .category = "Math",
                .summary = "Adds two float streams",
                .inputs = {{"in0", "float"}, {"in1", "float"}},
                .outputs = {{"out", "float"}},
                .parameters = {{"scale", "float", false, 1.0, ""}},
            },
            {
                .id = "blocks.analog.wfm_rcv",
                .name = "WFM Receive",
                .category = "Analog",
                .summary = "",
                .inputs = {{"in", "complex"}},
                .outputs = {{"out", "float"}},
                .parameters = {},
            },
        };
    }
};

class HttpApiTest : public ::testing::Test {
protected:
    class FakeRuntimeManager final : public gr4cp::runtime::RuntimeManager {
    public:
        void prepare(const gr4cp::domain::Session&) override {}
        void start(const gr4cp::domain::Session&) override {}
        void stop(const gr4cp::domain::Session&) override {}
        void destroy(const gr4cp::domain::Session& session) override { settings.erase(session.id); }

        void set_block_settings(const gr4cp::domain::Session& session,
                                const std::string& block_unique_name,
                                const gr::property_map& patch,
                                gr4cp::runtime::BlockSettingsMode mode) override {
            set_calls++;
            last_session_id = session.id;
            last_block = block_unique_name;
            last_mode = mode;
            last_patch = patch;
            if (set_exception) {
                std::rethrow_exception(set_exception);
            }
            auto& block_settings = settings[session.id][block_unique_name];
            for (const auto& [key, value] : patch) {
                block_settings.insert_or_assign(key, value);
            }
        }

        gr::property_map get_block_settings(const gr4cp::domain::Session& session,
                                            const std::string& block_unique_name) override {
            get_calls++;
            last_session_id = session.id;
            last_block = block_unique_name;
            if (get_exception) {
                std::rethrow_exception(get_exception);
            }
            return settings[session.id][block_unique_name];
        }

        int set_calls{};
        int get_calls{};
        std::string last_session_id;
        std::string last_block;
        gr4cp::runtime::BlockSettingsMode last_mode{gr4cp::runtime::BlockSettingsMode::Staged};
        gr::property_map last_patch;
        std::unordered_map<std::string, std::unordered_map<std::string, gr::property_map>> settings;
        std::exception_ptr set_exception;
        std::exception_ptr get_exception;
    };

    static std::string url_encode(std::string_view value) {
        std::ostringstream encoded;
        encoded << std::uppercase << std::hex;
        for (const char ch : value) {
            const auto byte = static_cast<unsigned char>(ch);
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' ||
                ch == '_' || ch == '.' || ch == '~') {
                encoded << ch;
            } else {
                encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
        }
        return encoded.str();
    }

    void wait_for_server_ready() {
        httplib::Client probe("127.0.0.1", port);
        for (int attempt = 0; attempt < 50; ++attempt) {
            if (const auto response = probe.Get("/healthz"); response && response->status == 200) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        FAIL() << "server did not become ready";
    }

    void SetUp() override {
        gr4cp::api::register_routes(server, service, block_catalog_service, block_settings_service);
        port = server.bind_to_any_port("127.0.0.1");
        ASSERT_GT(port, 0);
        server_thread = std::jthread([this]() { server.listen_after_bind(); });
        wait_for_server_ready();
        client = std::make_unique<httplib::Client>("127.0.0.1", port);
    }

    void TearDown() override {
        server.stop();
        server_thread.join();
    }

    static nlohmann::json parse_json(const httplib::Result& response) {
        EXPECT_TRUE(response);
        return nlohmann::json::parse(response->body);
    }

    static void expect_json_content_type(const httplib::Result& response) {
        ASSERT_TRUE(response);
        EXPECT_EQ(response->get_header_value("Content-Type"), "application/json");
    }

    nlohmann::json create_session_via_api() {
        const auto response = client->Post("/sessions", R"({"name":"demo","grc":"flowgraph"})", "application/json");
        EXPECT_TRUE(response);
        EXPECT_EQ(response->status, 201);
        return parse_json(response);
    }

    std::string create_running_session() {
        const auto created = create_session_via_api();
        const auto id = created["id"].get<std::string>();
        const auto started = client->Post(("/sessions/" + id + "/start").c_str(), "", "application/json");
        EXPECT_TRUE(started);
        EXPECT_EQ(started->status, 200);
        return id;
    }

    httplib::Server server;
    gr4cp::storage::InMemorySessionRepository repository;
    FakeRuntimeManager runtime_manager;
    TestBlockCatalogProvider block_catalog_provider;
    gr4cp::app::SessionService service{repository, runtime_manager};
    gr4cp::app::BlockSettingsService block_settings_service{repository, runtime_manager};
    gr4cp::app::BlockCatalogService block_catalog_service{block_catalog_provider};
    int port{};
    std::jthread server_thread;
    std::unique_ptr<httplib::Client> client;
};

TEST_F(HttpApiTest, PostSessionsSuccess) {
    const auto response = client->Post("/sessions", R"({"name":"demo","grc":"flowgraph"})", "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 201);
    expect_json_content_type(response);

    const auto body = parse_json(response);
    EXPECT_TRUE(body["id"].get<std::string>().starts_with("sess_"));
    EXPECT_EQ(body["name"], "demo");
    EXPECT_EQ(body["state"], "stopped");
    EXPECT_TRUE(body["last_error"].is_null());
    EXPECT_TRUE(body.contains("created_at"));
    EXPECT_TRUE(body.contains("updated_at"));
    EXPECT_FALSE(body.contains("grc_content"));
}

TEST_F(HttpApiTest, GetBlocksSuccess) {
    const auto response = client->Get("/blocks");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    expect_json_content_type(response);

    const auto body = parse_json(response);
    ASSERT_EQ(body.size(), 5U);
    EXPECT_EQ(body[0]["category"], "Analog");
    EXPECT_EQ(body[0]["name"], "WFM Receive");
    EXPECT_TRUE(body[0]["inputs"].is_array());
    EXPECT_TRUE(body[0]["outputs"].is_array());
    EXPECT_TRUE(body[0]["parameters"].is_array());
}

TEST_F(HttpApiTest, GetBlocksUsesDeterministicOrdering) {
    const auto response = client->Get("/blocks");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    const auto body = parse_json(response);
    ASSERT_EQ(body.size(), 5U);

    for (std::size_t index = 1; index < body.size(); ++index) {
        const auto& previous = body[index - 1];
        const auto& current = body[index];
        const auto previous_category = previous.at("category").get<std::string>();
        const auto current_category = current.at("category").get<std::string>();
        const auto previous_name = previous.at("name").get<std::string>();
        const auto current_name = current.at("name").get<std::string>();
        const auto previous_id = previous.at("id").get<std::string>();
        const auto current_id = current.at("id").get<std::string>();

        const bool in_order = previous_category < current_category ||
                              (previous_category == current_category &&
                               (previous_name < current_name ||
                                (previous_name == current_name && previous_id < current_id)));
        EXPECT_TRUE(in_order);
    }
}

TEST_F(HttpApiTest, GetBlockByIdSuccess) {
    const auto response = client->Get("/blocks/blocks.math.add_ff");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    expect_json_content_type(response);

    const auto body = parse_json(response);
    EXPECT_EQ(body["id"], "blocks.math.add_ff");
    EXPECT_EQ(body["name"], "Add");
    EXPECT_EQ(body["category"], "Math");
    ASSERT_EQ(body["inputs"].size(), 2U);
    ASSERT_EQ(body["parameters"].size(), 1U);
    EXPECT_EQ(body["parameters"][0]["default"], 1.0);
}

TEST_F(HttpApiTest, GetBlockByIdIncludesDynamicCollectionMetadata) {
    const auto response = client->Get("/blocks/blocks.math.add_dynamic");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);

    const auto body = parse_json(response);
    ASSERT_EQ(body["inputs"].size(), 1U);
    const auto& input = body["inputs"][0];
    EXPECT_EQ(input["cardinality_kind"], "dynamic");
    EXPECT_EQ(input["current_port_count"], 3);
    EXPECT_EQ(input["render_port_count"], 3);
    EXPECT_EQ(input["min_port_count"], 1);
    EXPECT_EQ(input["max_port_count"], 32);
    EXPECT_EQ(input["size_parameter"], "n_inputs");
    EXPECT_EQ(input["handle_name_template"], "in#${index}");
}

TEST_F(HttpApiTest, GetBlockByIdIncludesExtendedParameterMetadataWhenAvailable) {
    const auto response = client->Get("/blocks/blocks.math.add_ff");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);

    const auto body = parse_json(response);
    ASSERT_EQ(body["parameters"].size(), 1U);
    const auto& parameter = body["parameters"][0];
    EXPECT_EQ(parameter["summary"], "Scale factor");
    EXPECT_EQ(parameter["runtime_mutability"], "mutable");
    EXPECT_EQ(parameter["value_kind"], "scalar");
    EXPECT_EQ(parameter["ui_hint"], "advanced");
    EXPECT_FALSE(parameter.contains("enum_options"));
    EXPECT_FALSE(parameter.contains("enum_labels"));
    EXPECT_FALSE(parameter.contains("enum_source"));
    EXPECT_FALSE(parameter.contains("allow_custom_value"));
}

TEST_F(HttpApiTest, GetBlockByIdOmitsUnknownExtendedParameterMetadata) {
    const auto response = client->Get("/blocks/blocks.sources.signal_source_f");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);

    const auto body = parse_json(response);
    ASSERT_EQ(body["parameters"].size(), 1U);
    const auto& parameter = body["parameters"][0];
    EXPECT_FALSE(parameter.contains("runtime_mutability"));
    EXPECT_FALSE(parameter.contains("value_kind"));
    EXPECT_FALSE(parameter.contains("enum_options"));
    EXPECT_FALSE(parameter.contains("enum_labels"));
    EXPECT_FALSE(parameter.contains("enum_source"));
    EXPECT_FALSE(parameter.contains("ui_hint"));
    EXPECT_FALSE(parameter.contains("allow_custom_value"));
}

TEST_F(HttpApiTest, GetBlockByEncodedIdSuccess) {
    const auto id = "gr::blocks::math::Add<float32, std::plus<float32>>";
    const auto response = client->Get(("/blocks/" + url_encode(id)).c_str());

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    expect_json_content_type(response);

    const auto body = parse_json(response);
    EXPECT_EQ(body["id"], id);
    ASSERT_EQ(body["inputs"].size(), 2U);
    ASSERT_EQ(body["parameters"].size(), 1U);
}

TEST_F(HttpApiTest, ListedBlockIdsRoundTripIntoDetails) {
    const auto listed = client->Get("/blocks");
    ASSERT_TRUE(listed);
    ASSERT_EQ(listed->status, 200);
    const auto blocks = parse_json(listed);
    ASSERT_FALSE(blocks.empty());

    for (const auto& block : blocks) {
        const auto id = block["id"].get<std::string>();
        const auto detail = client->Get(("/blocks/" + url_encode(id)).c_str());
        ASSERT_TRUE(detail) << id;
        EXPECT_EQ(detail->status, 200) << id;
    }
}

TEST_F(HttpApiTest, GetBlockByIdNotFoundReturns404) {
    const auto response = client->Get("/blocks/blocks.missing");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 404);
    expect_json_content_type(response);
    const auto body = parse_json(response);
    EXPECT_EQ(body["error"]["code"], "not_found");
    EXPECT_FALSE(body["error"]["message"].get<std::string>().empty());
}

TEST_F(HttpApiTest, PostSessionsWithMissingGrcReturnsBadRequest) {
    const auto response = client->Post("/sessions", R"({"name":"demo"})", "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 400);
    expect_json_content_type(response);

    const auto body = parse_json(response);
    EXPECT_TRUE(body.contains("error"));
    EXPECT_EQ(body["error"]["code"], "validation_error");
    EXPECT_FALSE(body["error"]["message"].get<std::string>().empty());
}

TEST_F(HttpApiTest, PostSessionsWithMalformedJsonReturnsBadRequest) {
    const auto response = client->Post("/sessions", R"({"name":"demo","grc":)", "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 400);
    expect_json_content_type(response);

    const auto body = parse_json(response);
    EXPECT_TRUE(body.contains("error"));
    EXPECT_EQ(body["error"]["code"], "validation_error");
    EXPECT_EQ(body["error"]["message"], "request body must be valid JSON");
}

TEST_F(HttpApiTest, PostSessionsWithoutNameUsesEmptyString) {
    const auto response = client->Post("/sessions", R"({"grc":"flowgraph"})", "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 201);
    expect_json_content_type(response);
    EXPECT_EQ(parse_json(response)["name"], "");
}

TEST_F(HttpApiTest, GetSessionsWhenEmpty) {
    const auto response = client->Get("/sessions");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    expect_json_content_type(response);
    EXPECT_EQ(parse_json(response), nlohmann::json::array());
}

TEST_F(HttpApiTest, GetSessionsAfterCreate) {
    const auto created = client->Post("/sessions", R"({"name":"demo","grc":"flowgraph"})", "application/json");
    ASSERT_TRUE(created);

    const auto response = client->Get("/sessions");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    expect_json_content_type(response);

    const auto body = parse_json(response);
    ASSERT_EQ(body.size(), 1U);
    EXPECT_EQ(body[0]["name"], "demo");
    EXPECT_EQ(body[0]["state"], "stopped");
}

TEST_F(HttpApiTest, GetSessionByIdSuccess) {
    const auto created = parse_json(client->Post("/sessions", R"({"name":"demo","grc":"flowgraph"})", "application/json"));
    const auto response = client->Get(("/sessions/" + created["id"].get<std::string>()).c_str());

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    expect_json_content_type(response);
    const auto body = parse_json(response);
    EXPECT_EQ(body["id"], created["id"]);
    EXPECT_EQ(body["state"], "stopped");
    EXPECT_TRUE(body["last_error"].is_null());
}

TEST_F(HttpApiTest, GetSessionByIdNotFoundReturns404) {
    const auto response = client->Get("/sessions/sess_missing");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 404);
    expect_json_content_type(response);
    const auto body = parse_json(response);
    EXPECT_EQ(body["error"]["code"], "not_found");
    EXPECT_FALSE(body["error"]["message"].get<std::string>().empty());
}

TEST_F(HttpApiTest, StartSessionSuccess) {
    const auto created = parse_json(client->Post("/sessions", R"({"name":"demo","grc":"flowgraph"})", "application/json"));
    const auto response = client->Post(("/sessions/" + created["id"].get<std::string>() + "/start").c_str(), "", "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    expect_json_content_type(response);
    const auto body = parse_json(response);
    EXPECT_EQ(body["state"], "running");
    EXPECT_TRUE(body["last_error"].is_null());
}

TEST_F(HttpApiTest, StartSessionWhenAlreadyRunningReturnsConflict) {
    const auto created = parse_json(client->Post("/sessions", R"({"name":"demo","grc":"flowgraph"})", "application/json"));
    const auto path = "/sessions/" + created["id"].get<std::string>() + "/start";
    const auto first = client->Post(path.c_str(), "", "application/json");
    ASSERT_TRUE(first);

    const auto response = client->Post(path.c_str(), "", "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 409);
    expect_json_content_type(response);
    const auto body = parse_json(response);
    EXPECT_EQ(body["error"]["code"], "invalid_state");
    EXPECT_FALSE(body["error"]["message"].get<std::string>().empty());
}

TEST_F(HttpApiTest, StopSessionSuccess) {
    const auto created = parse_json(client->Post("/sessions", R"({"name":"demo","grc":"flowgraph"})", "application/json"));
    const auto id = created["id"].get<std::string>();
    ASSERT_TRUE(client->Post(("/sessions/" + id + "/start").c_str(), "", "application/json"));

    const auto response = client->Post(("/sessions/" + id + "/stop").c_str(), "", "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    expect_json_content_type(response);
    const auto body = parse_json(response);
    EXPECT_EQ(body["state"], "stopped");
    EXPECT_TRUE(body["last_error"].is_null());
}

TEST_F(HttpApiTest, StopSessionFromStoppedIsNoOpSuccess) {
    const auto created = parse_json(client->Post("/sessions", R"({"name":"demo","grc":"flowgraph"})", "application/json"));
    const auto response = client->Post(("/sessions/" + created["id"].get<std::string>() + "/stop").c_str(), "", "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    expect_json_content_type(response);
    const auto body = parse_json(response);
    EXPECT_EQ(body["state"], "stopped");
    EXPECT_TRUE(body["last_error"].is_null());
}

TEST_F(HttpApiTest, RestartSessionSuccess) {
    const auto created = parse_json(client->Post("/sessions", R"({"name":"demo","grc":"flowgraph"})", "application/json"));
    const auto response =
        client->Post(("/sessions/" + created["id"].get<std::string>() + "/restart").c_str(), "", "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    expect_json_content_type(response);
    const auto body = parse_json(response);
    EXPECT_EQ(body["state"], "running");
    EXPECT_TRUE(body["last_error"].is_null());
}

TEST_F(HttpApiTest, DeleteSessionSuccessReturnsNoContent) {
    const auto created = parse_json(client->Post("/sessions", R"({"name":"demo","grc":"flowgraph"})", "application/json"));
    const auto response = client->Delete(("/sessions/" + created["id"].get<std::string>()).c_str());

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 204);
    EXPECT_TRUE(response->body.empty());

    const auto missing = client->Get(("/sessions/" + created["id"].get<std::string>()).c_str());
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, 404);
    expect_json_content_type(missing);
}

TEST_F(HttpApiTest, DeleteSessionNotFoundReturns404) {
    const auto response = client->Delete("/sessions/sess_missing");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 404);
    expect_json_content_type(response);
    const auto body = parse_json(response);
    EXPECT_EQ(body["error"]["code"], "not_found");
    EXPECT_FALSE(body["error"]["message"].get<std::string>().empty());
}

TEST_F(HttpApiTest, PostBlockSettingsUsesStagedModeByDefault) {
    const auto id = create_running_session();

    const auto response = client->Post(("/sessions/" + id + "/blocks/sig0/settings").c_str(),
                                       R"({"frequency":1250.0,"amplitude":0.5})",
                                       "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    const auto body = parse_json(response);
    EXPECT_EQ(body["session_id"], id);
    EXPECT_EQ(body["block"], "sig0");
    EXPECT_EQ(body["applied_via"], "staged_settings");
    EXPECT_EQ(body["accepted"], true);
    EXPECT_EQ(runtime_manager.last_mode, gr4cp::runtime::BlockSettingsMode::Staged);
    EXPECT_EQ(runtime_manager.set_calls, 1);
    ASSERT_TRUE(runtime_manager.last_patch.contains("frequency"));
    EXPECT_EQ(runtime_manager.last_patch.at("frequency").value_or(0.0), 1250.0);
}

TEST_F(HttpApiTest, PostBlockSettingsImmediateModeUsesImmediateEndpoint) {
    const auto id = create_running_session();

    const auto response = client->Post(("/sessions/" + id + "/blocks/sig0/settings?mode=immediate").c_str(),
                                       R"({"enabled":true})",
                                       "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    EXPECT_EQ(parse_json(response)["applied_via"], "settings");
    EXPECT_EQ(runtime_manager.last_mode, gr4cp::runtime::BlockSettingsMode::Immediate);
}

TEST_F(HttpApiTest, GetBlockSettingsReturnsCurrentSettings) {
    const auto id = create_running_session();
    runtime_manager.settings[id]["sig0"] = gr::property_map{
        {"frequency", 1250.0},
        {"amplitude", 0.5},
        {"nested", gr::property_map{{"enabled", true}}},
    };

    const auto response = client->Get(("/sessions/" + id + "/blocks/sig0/settings").c_str());

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    const auto body = parse_json(response);
    EXPECT_EQ(body["settings"]["frequency"], 1250.0);
    EXPECT_EQ(body["settings"]["amplitude"], 0.5);
    EXPECT_EQ(body["settings"]["nested"]["enabled"], true);
}

TEST_F(HttpApiTest, PostBlockSettingsOnStoppedSessionReturnsConflict) {
    const auto created = create_session_via_api();

    const auto response = client->Post(("/sessions/" + created["id"].get<std::string>() + "/blocks/sig0/settings").c_str(),
                                       R"({"frequency":1250.0})",
                                       "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 409);
    EXPECT_EQ(parse_json(response)["error"]["code"], "invalid_state");
}

TEST_F(HttpApiTest, GetBlockSettingsOnStoppedSessionReturnsConflict) {
    const auto created = create_session_via_api();

    const auto response = client->Get(("/sessions/" + created["id"].get<std::string>() + "/blocks/sig0/settings").c_str());

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 409);
    EXPECT_EQ(parse_json(response)["error"]["code"], "invalid_state");
}

TEST_F(HttpApiTest, BlockSettingsUnknownSessionReturnsNotFound) {
    const auto response = client->Get("/sessions/sess_missing/blocks/sig0/settings");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 404);
    EXPECT_EQ(parse_json(response)["error"]["code"], "not_found");
}

TEST_F(HttpApiTest, BlockSettingsUnknownBlockReturnsNotFound) {
    const auto id = create_running_session();
    runtime_manager.set_exception =
        std::make_exception_ptr(gr4cp::runtime::BlockNotFoundError("block not found in running session: missing"));

    const auto response = client->Post(("/sessions/" + id + "/blocks/missing/settings").c_str(),
                                       R"({"frequency":1250.0})",
                                       "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 404);
    EXPECT_EQ(parse_json(response)["error"]["code"], "not_found");
}

TEST_F(HttpApiTest, BlockSettingsInvalidJsonBodyReturnsValidationError) {
    const auto id = create_running_session();

    const auto response = client->Post(("/sessions/" + id + "/blocks/sig0/settings").c_str(),
                                       R"({"frequency":)",
                                       "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 400);
    EXPECT_EQ(parse_json(response)["error"]["code"], "validation_error");
}

TEST_F(HttpApiTest, BlockSettingsUnsupportedArrayReturnsValidationError) {
    const auto id = create_running_session();

    const auto response = client->Post(("/sessions/" + id + "/blocks/sig0/settings").c_str(),
                                       R"({"values":[1,2,3]})",
                                       "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 400);
    EXPECT_EQ(parse_json(response)["error"]["code"], "validation_error");
}

TEST_F(HttpApiTest, BlockSettingsRuntimeFailureReturnsRuntimeError) {
    const auto id = create_running_session();
    runtime_manager.set_exception = std::make_exception_ptr(std::runtime_error("runtime manager unavailable"));

    const auto response = client->Post(("/sessions/" + id + "/blocks/sig0/settings").c_str(),
                                       R"({"frequency":1250.0})",
                                       "application/json");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 500);
    EXPECT_EQ(parse_json(response)["error"]["code"], "runtime_error");
}

TEST_F(HttpApiTest, BlockSettingsReplyTimeoutReturnsGatewayTimeout) {
    const auto id = create_running_session();
    runtime_manager.get_exception =
        std::make_exception_ptr(gr4cp::runtime::ReplyTimeoutError("timed out waiting for runtime reply from block sig0"));

    const auto response = client->Get(("/sessions/" + id + "/blocks/sig0/settings").c_str());

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 504);
    EXPECT_EQ(parse_json(response)["error"]["code"], "timeout");
}

TEST_F(HttpApiTest, BlockSettingsNestedObjectRoundTrips) {
    const auto id = create_running_session();

    const auto post_response = client->Post(("/sessions/" + id + "/blocks/sig0/settings").c_str(),
                                            R"({"nested":{"gain":3.0,"enabled":true}})",
                                            "application/json");
    ASSERT_TRUE(post_response);
    ASSERT_EQ(post_response->status, 200);

    const auto get_response = client->Get(("/sessions/" + id + "/blocks/sig0/settings").c_str());
    ASSERT_TRUE(get_response);
    ASSERT_EQ(get_response->status, 200);
    const auto body = parse_json(get_response);
    EXPECT_EQ(body["settings"]["nested"]["gain"], 3.0);
    EXPECT_EQ(body["settings"]["nested"]["enabled"], true);
}

}  // namespace
