#include "gr4cp/api/http_server.hpp"

#include <string>
#include <vector>
#include <string_view>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace gr4cp::api {

namespace {

using Json = nlohmann::json;

Json block_parameter_default_to_json(const domain::BlockParameterDefault& value) {
    return std::visit([](const auto& item) -> Json { return item; }, value);
}

Json block_port_to_json(const domain::BlockPortDescriptor& port) {
    auto body = Json{
        {"name", port.name},
        {"type", port.type},
        {"cardinality_kind", port.cardinality_kind == domain::BlockPortCardinalityKind::Dynamic ? "dynamic" : "fixed"},
    };

    if (port.current_port_count.has_value()) {
        body["current_port_count"] = *port.current_port_count;
    }
    if (port.render_port_count.has_value()) {
        body["render_port_count"] = *port.render_port_count;
    }
    if (port.min_port_count.has_value()) {
        body["min_port_count"] = *port.min_port_count;
    }
    if (port.max_port_count.has_value()) {
        body["max_port_count"] = *port.max_port_count;
    }
    if (port.size_parameter.has_value()) {
        body["size_parameter"] = *port.size_parameter;
    }
    if (port.handle_name_template.has_value()) {
        body["handle_name_template"] = *port.handle_name_template;
    }

    return body;
}

Json block_parameter_to_json(const domain::BlockParameterDescriptor& parameter) {
    auto body = Json{
        {"name", parameter.name},
        {"type", parameter.type},
        {"required", parameter.required},
        {"default", block_parameter_default_to_json(parameter.default_value)},
        {"summary", parameter.summary},
    };

    if (parameter.runtime_mutability.has_value()) {
        body["runtime_mutability"] = *parameter.runtime_mutability;
    }
    if (parameter.value_kind.has_value()) {
        body["value_kind"] = *parameter.value_kind;
    }
    if (parameter.enum_choices.has_value()) {
        body["enum_choices"] = *parameter.enum_choices;
    }
    if (parameter.enum_type.has_value()) {
        body["enum_type"] = *parameter.enum_type;
    }
    if (!parameter.enum_labels.empty()) {
        body["enum_labels"] = parameter.enum_labels;
    }
    if (parameter.enum_source.has_value()) {
        body["enum_source"] = *parameter.enum_source;
    }
    if (parameter.ui_hint.has_value()) {
        body["ui_hint"] = *parameter.ui_hint;
    }
    if (parameter.allow_custom_value.has_value()) {
        body["allow_custom_value"] = *parameter.allow_custom_value;
    }

    return body;
}

Json block_to_json(const domain::BlockDescriptor& block) {
    auto inputs = Json::array();
    for (const auto& input : block.inputs) {
        inputs.push_back(block_port_to_json(input));
    }

    auto outputs = Json::array();
    for (const auto& output : block.outputs) {
        outputs.push_back(block_port_to_json(output));
    }

    auto parameters = Json::array();
    for (const auto& parameter : block.parameters) {
        parameters.push_back(block_parameter_to_json(parameter));
    }

    return Json{
        {"id", block.id},
        {"name", block.name},
        {"category", block.category},
        {"summary", block.summary},
        {"inputs", std::move(inputs)},
        {"outputs", std::move(outputs)},
        {"parameters", std::move(parameters)},
    };
}

Json blocks_to_json(const std::vector<domain::BlockDescriptor>& blocks) {
    auto body = Json::array();
    for (const auto& block : blocks) {
        body.push_back(block_to_json(block));
    }
    return body;
}

Json session_to_json(const domain::Session& session) {
    return Json{
        {"id", session.id},
        {"name", session.name},
        {"state", domain::to_string(session.state)},
        {"created_at", domain::format_timestamp_utc(session.created_at)},
        {"updated_at", domain::format_timestamp_utc(session.updated_at)},
        {"last_error", session.last_error ? Json(*session.last_error) : Json(nullptr)},
    };
}

Json sessions_to_json(const std::vector<domain::Session>& sessions) {
    auto body = Json::array();
    for (const auto& session : sessions) {
        body.push_back(session_to_json(session));
    }
    return body;
}

Json block_settings_update_to_json(const app::BlockSettingsUpdateResult& result) {
    return Json{
        {"session_id", result.session_id},
        {"block", result.block},
        {"applied_via", result.applied_via},
        {"accepted", result.accepted},
    };
}

void set_json_response(httplib::Response& response, const Json& body, const int status = 200) {
    response.status = status;
    response.set_content(body.dump(), "application/json");
}

void set_error_response(httplib::Response& response,
                        const int status,
                        const std::string& code,
                        const std::string& message) {
    set_json_response(response, Json{{"error", {{"code", code}, {"message", message}}}}, status);
}

void set_no_content_response(httplib::Response& response) {
    response.status = 204;
    response.body.clear();
}

Json parse_json_body(const httplib::Request& request) {
    try {
        return Json::parse(request.body);
    } catch (const Json::parse_error&) {
        throw app::ValidationError("request body must be valid JSON");
    }
}

app::BlockSettingsService::Json parse_settings_body(const httplib::Request& request) {
    return parse_json_body(request);
}

runtime::BlockSettingsMode parse_settings_mode(const httplib::Request& request) {
    const auto mode = request.get_param_value("mode");
    if (mode.empty() || mode == "staged") {
        return runtime::BlockSettingsMode::Staged;
    }
    if (mode == "immediate") {
        return runtime::BlockSettingsMode::Immediate;
    }
    throw app::ValidationError("mode must be 'staged' or 'immediate'");
}

std::string get_required_grc(const Json& body) {
    const auto it = body.find("grc");
    if (it == body.end() || !it->is_string() || it->get_ref<const std::string&>().empty()) {
        throw app::ValidationError("grc must be a non-empty string");
    }
    return it->get<std::string>();
}

std::string get_optional_name(const Json& body) {
    const auto it = body.find("name");
    if (it == body.end() || it->is_null()) {
        return "";
    }
    if (!it->is_string()) {
        throw app::ValidationError("name must be a string");
    }
    return it->get<std::string>();
}

std::string decode_percent_encoded(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '%' && index + 2 < value.size()) {
            const auto hex = value.substr(index + 1, 2);
            const auto byte = static_cast<char>(std::stoi(std::string(hex), nullptr, 16));
            decoded.push_back(byte);
            index += 2;
        } else if (ch == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(ch);
        }
    }
    return decoded;
}

void handle_exception(httplib::Response& response) {
    try {
        throw;
    } catch (const app::ValidationError& error) {
        set_error_response(response, 400, "validation_error", error.what());
    } catch (const app::NotFoundError& error) {
        set_error_response(response, 404, "not_found", error.what());
    } catch (const app::InvalidStateError& error) {
        set_error_response(response, 409, "invalid_state", error.what());
    } catch (const app::RuntimeError& error) {
        set_error_response(response, 500, "runtime_error", error.what());
    } catch (const app::TimeoutError& error) {
        set_error_response(response, 504, "timeout", error.what());
    } catch (const catalog::CatalogLoadError& error) {
        set_error_response(response, 500, "catalog_error", error.what());
    } catch (const std::exception&) {
        set_error_response(response, 500, "internal_error", "internal server error");
    } catch (...) {
        set_error_response(response, 500, "internal_error", "internal server error");
    }
}

template <typename Action>
void execute(httplib::Response& response, Action&& action) {
    try {
        std::forward<Action>(action)();
    } catch (...) {
        handle_exception(response);
    }
}

}  // namespace

void register_routes(httplib::Server& server,
                     app::SessionService& session_service,
                     app::BlockCatalogService& block_catalog_service,
                     app::BlockSettingsService& block_settings_service) {
    server.Get("/healthz", [](const httplib::Request&, httplib::Response& response) {
        set_json_response(response, Json{{"ok", true}});
    });

    server.Get("/blocks", [&block_catalog_service](const httplib::Request&, httplib::Response& response) {
        execute(response, [&]() {
            set_json_response(response, blocks_to_json(block_catalog_service.list()));
        });
    });

    server.Get(R"(/blocks/(.+))",
               [&block_catalog_service](const httplib::Request& request, httplib::Response& response) {
                   execute(response, [&]() {
                       set_json_response(response,
                                         block_to_json(block_catalog_service.get(
                                             decode_percent_encoded(request.matches[1].str()))));
                   });
               });

    server.Post("/sessions", [&session_service](const httplib::Request& request, httplib::Response& response) {
        execute(response, [&]() {
            const auto body = parse_json_body(request);
            const auto session = session_service.create(get_optional_name(body), get_required_grc(body));
            set_json_response(response, session_to_json(session), 201);
        });
    });

    server.Get("/sessions", [&session_service](const httplib::Request&, httplib::Response& response) {
        execute(response, [&]() {
            set_json_response(response, sessions_to_json(session_service.list()));
        });
    });

    server.Get(R"(/sessions/([A-Za-z0-9_]+))",
               [&session_service](const httplib::Request& request, httplib::Response& response) {
                   execute(response, [&]() {
                       set_json_response(response, session_to_json(session_service.get(request.matches[1].str())));
                   });
               });

    server.Delete(R"(/sessions/([A-Za-z0-9_]+))",
                  [&session_service](const httplib::Request& request, httplib::Response& response) {
                      execute(response, [&]() {
                          session_service.remove(request.matches[1].str());
                          set_no_content_response(response);
                      });
                  });

    server.Post(R"(/sessions/([A-Za-z0-9_]+)/start)",
                [&session_service](const httplib::Request& request, httplib::Response& response) {
                    execute(response, [&]() {
                        set_json_response(response,
                                          session_to_json(session_service.start(request.matches[1].str())));
                    });
                });

    server.Post(R"(/sessions/([A-Za-z0-9_]+)/stop)",
                [&session_service](const httplib::Request& request, httplib::Response& response) {
                    execute(response, [&]() {
                        set_json_response(response,
                                          session_to_json(session_service.stop(request.matches[1].str())));
                    });
                });

    server.Post(R"(/sessions/([A-Za-z0-9_]+)/restart)",
                [&session_service](const httplib::Request& request, httplib::Response& response) {
                    execute(response, [&]() {
                        set_json_response(response,
                                          session_to_json(session_service.restart(request.matches[1].str())));
                    });
                });

    server.Post(R"(/sessions/([A-Za-z0-9_]+)/blocks/(.+)/settings)",
                [&block_settings_service](const httplib::Request& request, httplib::Response& response) {
                    execute(response, [&]() {
                        const auto session_id = request.matches[1].str();
                        const auto block_unique_name = decode_percent_encoded(request.matches[2].str());
                        const auto result = block_settings_service.update(
                            session_id,
                            block_unique_name,
                            parse_settings_body(request),
                            parse_settings_mode(request));
                        set_json_response(response, block_settings_update_to_json(result));
                    });
                });

    server.Get(R"(/sessions/([A-Za-z0-9_]+)/blocks/(.+)/settings)",
               [&block_settings_service](const httplib::Request& request, httplib::Response& response) {
                   execute(response, [&]() {
                       const auto session_id = request.matches[1].str();
                       const auto block_unique_name = decode_percent_encoded(request.matches[2].str());
                       set_json_response(response,
                                         Json{{"settings", block_settings_service.get(session_id, block_unique_name)}});
                   });
               });
}

}  // namespace gr4cp::api
