#include "gr4cp/cli/cli.hpp"

#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace gr4cp::cli {

namespace {

using Json = nlohmann::json;

constexpr std::string_view kDefaultUrl = "http://127.0.0.1:8080";

struct ParsedUrl {
    std::string scheme{"http"};
    std::string host;
    int port{80};
};

struct CommandContext {
    std::string url{std::string(kDefaultUrl)};
};

class CliError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::string usage() {
    return "usage:\n"
           "  gr4cp-cli sessions create --file <path> [--name <name>] [--url <url>]\n"
           "  gr4cp-cli sessions list [--url <url>]\n"
           "  gr4cp-cli sessions get <id> [--url <url>]\n"
           "  gr4cp-cli sessions start <id> [--url <url>]\n"
           "  gr4cp-cli sessions stop <id> [--url <url>]\n"
           "  gr4cp-cli sessions restart <id> [--url <url>]\n"
           "  gr4cp-cli sessions delete <id> [--url <url>]\n";
}

ParsedUrl parse_url(const std::string& url) {
    const std::string prefix = "http://";
    if (!url.starts_with(prefix)) {
        throw CliError("only http:// URLs are supported");
    }

    const auto host_port = url.substr(prefix.size());
    if (host_port.empty()) {
        throw CliError("server URL must include a host");
    }

    const auto colon = host_port.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon == host_port.size() - 1) {
        throw CliError("server URL must be in the form http://host:port");
    }

    ParsedUrl parsed;
    parsed.host = host_port.substr(0, colon);

    try {
        parsed.port = std::stoi(host_port.substr(colon + 1));
    } catch (const std::exception&) {
        throw CliError("server URL port must be numeric");
    }

    return parsed;
}

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw CliError("failed to read file: " + path);
    }

    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

std::string pretty_json(const Json& body) {
    return body.dump(2);
}

std::optional<std::string> find_error_message(const httplib::Result& response) {
    if (!response || response->body.empty()) {
        return std::nullopt;
    }

    try {
        const auto raw_body = std::string(response->body);
        const auto body = Json::parse(raw_body);
        const auto error = body.find("error");
        if (error == body.end() || !error->is_object()) {
            return std::nullopt;
        }
        const auto code = error->find("code");
        const auto message = error->find("message");
        if (code != error->end() && message != error->end() && code->is_string() && message->is_string()) {
            return code->get<std::string>() + ": " + message->get<std::string>();
        }
    } catch (const Json::parse_error&) {
    }

    return std::nullopt;
}

const httplib::Response& require_response(const httplib::Result& response) {
    if (response) {
        return *response;
    }

    throw CliError("request failed");
}

Json parse_success_json(const httplib::Response& response) {
    const auto raw_body = std::string(response.body);
    return Json::parse(raw_body);
}

httplib::Result perform_request(httplib::Client& client,
                                const std::string& method,
                                const std::string& path,
                                const std::optional<Json>& body = std::nullopt) {
    if (method == "GET") {
        return client.Get(path);
    }
    if (method == "DELETE") {
        return client.Delete(path);
    }
    if (method == "POST") {
        const auto payload = body ? body->dump() : "";
        return client.Post(path, payload, "application/json");
    }

    throw CliError("unsupported method");
}

Json request_json(httplib::Client& client,
                  const std::string& method,
                  const std::string& path,
                  const std::optional<Json>& body = std::nullopt) {
    const auto response = perform_request(client, method, path, body);
    const auto& checked = require_response(response);
    if (checked.status >= 200 && checked.status < 300) {
        if (checked.body.empty()) {
            return Json{};
        }
        return parse_success_json(checked);
    }

    if (const auto error = find_error_message(response); error.has_value()) {
        throw CliError(*error);
    }
    throw CliError("server returned HTTP " + std::to_string(checked.status));
}

void request_no_content(httplib::Client& client, const std::string& method, const std::string& path) {
    const auto response = perform_request(client, method, path);
    const auto& checked = require_response(response);
    if (checked.status == 204) {
        return;
    }
    if (const auto error = find_error_message(response); error.has_value()) {
        throw CliError(*error);
    }
    throw CliError("server returned HTTP " + std::to_string(checked.status));
}

CommandContext parse_common_options(std::span<const std::string_view> args, std::size_t start_index) {
    CommandContext context;

    for (std::size_t i = start_index; i < args.size(); ++i) {
        if (args[i] == "--url") {
            if (i + 1 >= args.size()) {
                throw CliError("--url requires a value");
            }
            context.url = std::string(args[++i]);
            continue;
        }
        throw CliError("unknown option: " + std::string(args[i]));
    }

    return context;
}

httplib::Client& make_client(const std::string& url) {
    const auto parsed = parse_url(url);
    // The packaged cpp-httplib build in this environment crashes in Client teardown.
    // This CLI is a short-lived process, so leaking the client is the smaller compromise.
    return *(new httplib::Client(parsed.host, parsed.port));
}

int run_sessions(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    if (args.size() < 2 || args[0] != "sessions") {
        err << usage();
        return 1;
    }

    const auto command = args[1];

    try {
        if (command == "create") {
            std::string file_path;
            std::string name;
            auto context = CommandContext{};

            for (std::size_t i = 2; i < args.size(); ++i) {
                if (args[i] == "--file") {
                    if (i + 1 >= args.size()) {
                        throw CliError("--file requires a value");
                    }
                    file_path = std::string(args[++i]);
                } else if (args[i] == "--name") {
                    if (i + 1 >= args.size()) {
                        throw CliError("--name requires a value");
                    }
                    name = std::string(args[++i]);
                } else if (args[i] == "--url") {
                    if (i + 1 >= args.size()) {
                        throw CliError("--url requires a value");
                    }
                    context.url = std::string(args[++i]);
                } else {
                    throw CliError("unknown option: " + std::string(args[i]));
                }
            }

            if (file_path.empty()) {
                throw CliError("create requires --file");
            }

            auto& client = make_client(context.url);
            const auto body = Json{{"name", name}, {"grc", read_file(file_path)}};
            out << pretty_json(request_json(client, "POST", "/sessions", std::optional<Json>{body})) << '\n';
            return 0;
        }

        if (command == "list") {
            const auto context = parse_common_options(args, 2);
            auto& client = make_client(context.url);
            out << pretty_json(request_json(client, "GET", "/sessions")) << '\n';
            return 0;
        }

        if (command == "get" || command == "start" || command == "stop" || command == "restart" ||
            command == "delete") {
            if (args.size() < 3) {
                throw CliError(std::string(command) + " requires a session id");
            }

            const auto id = std::string(args[2]);
            const auto context = parse_common_options(args, 3);
            auto& client = make_client(context.url);

            if (command == "get") {
                out << pretty_json(request_json(client, "GET", "/sessions/" + id)) << '\n';
                return 0;
            }
            if (command == "start") {
                out << pretty_json(request_json(client, "POST", "/sessions/" + id + "/start")) << '\n';
                return 0;
            }
            if (command == "stop") {
                out << pretty_json(request_json(client, "POST", "/sessions/" + id + "/stop")) << '\n';
                return 0;
            }
            if (command == "restart") {
                out << pretty_json(request_json(client, "POST", "/sessions/" + id + "/restart")) << '\n';
                return 0;
            }

            request_no_content(client, "DELETE", "/sessions/" + id);
            out << "deleted " << id << '\n';
            return 0;
        }

        throw CliError("unknown sessions command: " + std::string(command));
    } catch (const CliError& error) {
        err << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        err << error.what() << '\n';
        return 1;
    }
}

}  // namespace

int run(const std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << usage();
        return 1;
    }

    return run_sessions(args, out, err);
}

}  // namespace gr4cp::cli
