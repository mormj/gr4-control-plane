#include "gr4cp/runtime/gr4_runtime_manager.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/YamlPmt.hpp>

namespace gr4cp::runtime {

namespace {

template<typename TResult>
bool connection_succeeded(const TResult& result) {
    if constexpr (requires { result.has_value(); }) {
        return result.has_value();
    } else if constexpr (std::is_enum_v<std::remove_cvref_t<TResult>>) {
        using raw_t = std::underlying_type_t<std::remove_cvref_t<TResult>>;
        return static_cast<raw_t>(result) == 0;
    } else {
        return static_cast<bool>(result);
    }
}

template<typename TResult>
std::string connection_error_message(const TResult& result) {
    if constexpr (requires { result.error().message; }) {
        return result.error().message;
    } else {
        return "failed to connect ports";
    }
}

std::vector<std::filesystem::path> split_plugin_directories(std::string_view paths) {
    std::vector<std::filesystem::path> directories;
    std::size_t start = 0;
    while (start <= paths.size()) {
        const auto end = paths.find(':', start);
        const auto token = paths.substr(start, end == std::string_view::npos ? paths.size() - start : end - start);
        if (!token.empty()) {
            directories.emplace_back(token);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return directories;
}

void prepend_library_search_path(const std::vector<std::filesystem::path>& plugin_directories) {
#if !defined(_WIN32)
    std::string value;
    for (const auto& directory : plugin_directories) {
        if (!value.empty()) {
            value += ":";
        }
        value += directory.string();
    }
    if (const char* existing = std::getenv("LD_LIBRARY_PATH"); existing != nullptr && *existing != '\0') {
        if (!value.empty()) {
            value += ":";
        }
        value += existing;
    }
    if (!value.empty()) {
        ::setenv("LD_LIBRARY_PATH", value.c_str(), 1);
    }
#else
    (void)plugin_directories;
#endif
}

void configure_plugin_environment(const std::vector<std::filesystem::path>& plugin_directories) {
    std::string value;
    for (const auto& directory : plugin_directories) {
        if (!value.empty()) {
            value += ":";
        }
        value += directory.string();
    }
    if (!value.empty()) {
        ::setenv("GNURADIO4_PLUGIN_DIRECTORIES", value.c_str(), 1);
    }
    prepend_library_search_path(plugin_directories);
}

bool is_shared_library(const std::filesystem::path& path) {
#if defined(__APPLE__)
    constexpr auto extension = ".dylib";
#elif defined(_WIN32)
    constexpr auto extension = ".dll";
#else
    constexpr auto extension = ".so";
#endif
    return path.extension() == extension;
}

bool is_shared_block_library(const std::filesystem::path& path) {
    return is_shared_library(path) && path.filename().string().find("Shared") != std::string::npos;
}

void preload_support_libraries(const std::vector<std::filesystem::path>& plugin_directories) {
#if defined(__unix__) || defined(__APPLE__)
    static std::vector<void*> handles;
    static std::set<std::string> loaded_libraries;

    auto should_preload = [](const std::filesystem::path& path) {
        const auto name = path.filename().string();
        return name == "libgnuradio-blocklib-core.so" || name == "libgnuradio-plugin.so";
    };

    for (const auto& directory : plugin_directories) {
        if (!std::filesystem::is_directory(directory)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || !should_preload(entry.path())) {
                continue;
            }
            const auto library = entry.path().string();
            if (!loaded_libraries.insert(library).second) {
                continue;
            }
            void* handle = ::dlopen(library.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (handle == nullptr) {
                loaded_libraries.erase(library);
                const char* error = ::dlerror();
                throw std::runtime_error("failed to preload GNU Radio 4 support library " + library +
                                         (error != nullptr ? ": " + std::string(error) : ""));
            }
            handles.push_back(handle);
        }
    }
#else
    (void)plugin_directories;
#endif
}

void register_default_schedulers() {
    auto& registry = gr::globalSchedulerRegistry();
    (void)registry.insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreadedBlocking>>("=gr::scheduler::SimpleSingle");
    (void)registry.insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded>>("=gr::scheduler::SimpleMulti");
}

std::string module_name_from_library(const std::filesystem::path& path) {
    std::string name = path.filename().string();
    if (name.rfind("lib", 0) == 0) {
        name = name.substr(3);
    }
    if (name.ends_with(".so")) {
        name.resize(name.size() - 3);
    } else if (name.ends_with(".dylib")) {
        name.resize(name.size() - 6);
    }
    if (name.ends_with("Shared")) {
        name.resize(name.size() - 6);
    }
    return name;
}

void bootstrap_shared_block_libraries(const std::vector<std::filesystem::path>& plugin_directories) {
#if defined(__unix__) || defined(__APPLE__)
    static std::vector<void*> handles;
    static std::set<std::string> retained;

    using init_fn_t = std::size_t (*)(gr::BlockRegistry&);

    for (const auto& directory : plugin_directories) {
        if (!std::filesystem::is_directory(directory)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || !is_shared_block_library(entry.path())) {
                continue;
            }

            const auto library_string = entry.path().string();
            if (!retained.insert(library_string).second) {
                continue;
            }

            void* handle = ::dlopen(library_string.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (handle == nullptr) {
                retained.erase(library_string);
                const char* error = ::dlerror();
                throw std::runtime_error("failed to load GNU Radio 4 shared block library " + library_string +
                                         (error != nullptr ? ": " + std::string(error) : ""));
            }

            const auto symbol_name = std::string("gr_blocklib_init_module_") + module_name_from_library(entry.path());
            auto* init_fn = reinterpret_cast<init_fn_t>(::dlsym(handle, symbol_name.c_str()));
            if (init_fn == nullptr) {
                handles.push_back(handle);
                continue;
            }

            try {
                (void)init_fn(gr::globalBlockRegistry());
            } catch (const std::exception& error) {
                throw std::runtime_error("failed to initialize GNU Radio 4 shared block library " + library_string +
                                         ": " + error.what());
            }
            handles.push_back(handle);
        }
    }
#else
    (void)plugin_directories;
#endif
}

void ensure_runtime_environment(const std::vector<std::filesystem::path>& plugin_directories) {
    static std::once_flag once;
    static std::optional<std::string> bootstrap_error;

    std::call_once(once, [&]() {
        try {
            if (plugin_directories.empty()) {
                throw std::runtime_error("no GNU Radio 4 plugin directories are configured");
            }
            configure_plugin_environment(plugin_directories);
            preload_support_libraries(plugin_directories);
            register_default_schedulers();
            bootstrap_shared_block_libraries(plugin_directories);
            auto& loader = gr::globalPluginLoader();
            (void)loader.availableBlocks();
        } catch (const std::exception& error) {
            bootstrap_error = error.what();
        }
    });

    if (bootstrap_error.has_value()) {
        throw std::runtime_error("GNU Radio 4 runtime initialization failed: " + *bootstrap_error);
    }
}

std::runtime_error runtime_error(const domain::Session& session, std::string_view phase, const std::string& detail) {
    return std::runtime_error(std::format("session {} {} failed: {}", session.id, phase, detail));
}

std::string next_request_id() {
    thread_local std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned long long> distribution;
    return std::format("req_{:016x}", distribution(generator));
}

std::optional<std::string> value_to_string(const gr::pmt::Value& value) {
    std::optional<std::string> result;
    gr::pmt::ValueVisitor([&result](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::same_as<T, std::pmr::string> || std::same_as<T, std::string>) {
            result = std::string(item);
        } else if constexpr (std::same_as<T, std::string_view>) {
            result = std::string(item);
        }
    }).visit(value);
    return result;
}

std::string describe_runtime_block(const std::shared_ptr<gr::BlockModel>& block) {
    std::string description(block->uniqueName());

    if (const auto name = std::string(block->name()); !name.empty() && name != description) {
        description += std::format(" (name={})", name);
    }

    if (const auto type_name = std::string(block->typeName()); !type_name.empty()) {
        description += std::format(" [type={}]", type_name);
    }

    if (const auto& settings = block->settings().get(); settings.contains("name")) {
        if (const auto settings_name = value_to_string(settings.at("name")); settings_name.has_value() && !settings_name->empty() &&
                                                                           *settings_name != std::string(block->uniqueName()) &&
                                                                           *settings_name != std::string(block->name())) {
            description += std::format(" [settings.name={}]", *settings_name);
        }
    }

    return description;
}

std::string summarize_runtime_blocks(const gr::BlockModel& root) {
    if (root.blocks().empty()) {
        return "<none>";
    }

    std::string result;
    bool first = true;
    for (const auto& block : root.blocks()) {
        if (!first) {
            result += ", ";
        }
        result += describe_runtime_block(block);
        first = false;
    }
    return result;
}

BlockNotFoundError make_block_not_found_error(const gr::BlockModel& root, std::string_view block_unique_name) {
    return BlockNotFoundError(std::format("block not found in running session: {}; available runtime blocks: {}",
                                          block_unique_name,
                                          summarize_runtime_blocks(root)));
}

std::shared_ptr<gr::BlockModel> resolve_runtime_block(const gr::BlockModel& root, std::string_view requested_block_target) {
    for (const auto& block : root.blocks()) {
        if (block->name() == requested_block_target) {
            return block;
        }
    }

    for (const auto& block : root.blocks()) {
        if (block->uniqueName() == requested_block_target) {
            return block;
        }
    }

    for (const auto& block : root.blocks()) {
        const auto& settings = block->settings().get();
        if (!settings.contains("name")) {
            continue;
        }
        const auto settings_name = value_to_string(settings.at("name"));
        if (settings_name.has_value() && *settings_name == requested_block_target) {
            return block;
        }
    }

    return nullptr;
}

void normalize_block(gr::property_map& block) {
    std::optional<std::string> studio_node_id;
    if (const auto it = block.find("id"); it != block.end()) {
        studio_node_id = value_to_string(it->second);
    }

    if (const auto it = block.find("block"); it != block.end()) {
        if (const auto block_type = value_to_string(it->second); block_type.has_value() && !block_type->empty()) {
            block["id"] = *block_type;
        }
    }

    if (!block.contains("id")) {
        if (const auto it = block.find("block_type"); it != block.end()) {
            if (const auto block_type = value_to_string(it->second); block_type.has_value() && !block_type->empty()) {
                block["id"] = *block_type;
            }
        }
    }

    gr::property_map parameters;
    bool needs_parameters_write = false;

    if (const auto it = block.find("parameters"); it != block.end()) {
        if (const auto* existing = it->second.get_if<gr::property_map>(); existing != nullptr) {
            parameters = *existing;
        }
    }

    if (const auto it = block.find("raw_parameters"); it != block.end()) {
        if (const auto* raw = it->second.get_if<gr::property_map>(); raw != nullptr) {
            for (const auto& [key, value] : *raw) {
                if (!parameters.contains(key)) {
                    parameters.insert_or_assign(key, value);
                    needs_parameters_write = true;
                }
            }
        }
    }

    if (!parameters.contains("name")) {
        if (studio_node_id.has_value() && !studio_node_id->empty()) {
            parameters["name"] = *studio_node_id;
            needs_parameters_write = true;
        }
    }

    if (!parameters.contains("name")) {
        for (const auto* candidate_key : {"instance_name", "name"}) {
            const auto it = block.find(candidate_key);
            if (it == block.end()) {
                continue;
            }
            if (const auto name = value_to_string(it->second); name.has_value() && !name->empty()) {
                parameters["name"] = *name;
                needs_parameters_write = true;
                break;
            }
        }
    }

    if (needs_parameters_write || !parameters.empty()) {
        block["parameters"] = parameters;
    }
}

bool normalize_connection(gr::pmt::Value& connection) {
    auto normalize_port = [](const std::string& token) -> gr::pmt::Value {
        if (token == "in" || token == "out") {
            return std::int64_t{0};
        }
        if (!token.empty() && std::all_of(token.begin(), token.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
            return std::int64_t{std::stoll(token)};
        }
        if (token.size() > 2 && (token.rfind("in", 0) == 0 || token.rfind("out", 0) == 0)) {
            const auto suffix = token.substr(token.rfind('t') + 1);
            if (!suffix.empty() && std::all_of(suffix.begin(), suffix.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                return std::int64_t{std::stoll(suffix)};
            }
        }
        return token;
    };

    if (auto* sequence = connection.get_if<gr::Tensor<gr::pmt::Value>>(); sequence != nullptr) {
        bool changed = false;
        if (sequence->size() >= 4) {
            for (const std::size_t index : {std::size_t{1}, std::size_t{3}}) {
                if (const auto token = value_to_string((*sequence)[index]); token.has_value()) {
                    const auto normalized = normalize_port(*token);
                    changed = changed || (normalized != (*sequence)[index]);
                    (*sequence)[index] = normalized;
                }
            }
        }
        return changed;
    }

    const auto* map = connection.get_if<gr::property_map>();
    if (map == nullptr) {
        return false;
    }

    auto get_required_string = [&](std::string_view key) -> std::optional<std::string> {
        const auto it = map->find(std::string(key));
        if (it == map->end()) {
            return std::nullopt;
        }
        return value_to_string(it->second);
    };

    const auto source_block = get_required_string("source_block");
    const auto source_port = get_required_string("source_port_token");
    const auto dest_block = get_required_string("dest_block");
    const auto dest_port = get_required_string("dest_port_token");
    if (!source_block.has_value() || !source_port.has_value() || !dest_block.has_value() || !dest_port.has_value()) {
        return false;
    }

    gr::Tensor<gr::pmt::Value> sequence;
    sequence.reserve(4);
    sequence.push_back(*source_block);
    sequence.push_back(normalize_port(*source_port));
    sequence.push_back(*dest_block);
    sequence.push_back(normalize_port(*dest_port));
    connection = std::move(sequence);
    return true;
}

std::string normalize_graph_content(std::string_view content) {
    const auto parsed = gr::pmt::yaml::deserialize(content);
    if (!parsed.has_value()) {
        return std::string(content);
    }

    auto root = *parsed;
    bool changed = false;

    if (auto it = root.find("blocks"); it != root.end()) {
        if (auto* blocks = it->second.get_if<gr::Tensor<gr::pmt::Value>>(); blocks != nullptr) {
            for (auto& block_value : *blocks) {
                if (auto* block = block_value.get_if<gr::property_map>(); block != nullptr) {
                    const auto before = *block;
                    normalize_block(*block);
                    changed = changed || (*block != before);
                }
            }
        }
    }

    if (auto it = root.find("connections"); it != root.end()) {
        if (auto* connections = it->second.get_if<gr::Tensor<gr::pmt::Value>>(); connections != nullptr) {
            for (auto& connection : *connections) {
                changed = normalize_connection(connection) || changed;
            }
        }
    }

    return changed ? gr::pmt::yaml::serialize(root) : std::string(content);
}

template <gr::message::Command Command>
gr::Message send_block_settings_request(gr::BlockModel& scheduler_block,
                                        std::string_view block_unique_name,
                                        std::string_view endpoint,
                                        gr::property_map payload,
                                        std::chrono::milliseconds timeout) {
    gr::MsgPortOutBuiltin request_port;
    gr::MsgPortInBuiltin response_port;

    if (auto connection = request_port.connect(*scheduler_block.msgIn); !connection_succeeded(connection)) {
        throw std::runtime_error(std::format("failed to connect runtime request port: {}", connection_error_message(connection)));
    }
    if (auto connection = scheduler_block.msgOut->connect(response_port); !connection_succeeded(connection)) {
        throw std::runtime_error(std::format("failed to connect runtime response port: {}", connection_error_message(connection)));
    }

    const auto request_id = next_request_id();
    gr::sendMessage<Command>(request_port, block_unique_name, endpoint, std::move(payload), request_id);

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto available = response_port.streamReader().available();
        if (available == 0UZ) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        gr::ReaderSpanLike auto replies = response_port.streamReader().get<gr::SpanReleasePolicy::ProcessAll>(available);
        for (const auto& reply : replies) {
            if (reply.clientRequestID != request_id || reply.endpoint != endpoint || reply.serviceName != block_unique_name) {
                continue;
            }
            return reply;
        }
    }

    throw ReplyTimeoutError(std::format("timed out waiting for runtime reply from block {}", block_unique_name));
}

gr::property_map parse_settings_reply(const gr::Message& reply, std::string_view block_unique_name, std::string_view endpoint) {
    if (!reply.data.has_value()) {
        throw std::runtime_error(std::format("runtime returned error for block {} {}: {}", block_unique_name, endpoint, reply.data.error().message));
    }
    return reply.data.value();
}

}  // namespace

struct Gr4RuntimeManager::Execution {
    std::shared_ptr<gr::SchedulerModel> scheduler;
    bool running{false};
};

std::string default_scheduler_alias() {
    const auto available = gr::globalPluginLoader().availableSchedulers();
    const auto preferred = std::string("gr::scheduler::SimpleSingle");
    if (std::ranges::find(available, preferred) != available.end()) {
        return preferred;
    }
    if (!available.empty()) {
        return available.front();
    }
    return preferred;
}

Gr4RuntimeManager::Gr4RuntimeManager(std::vector<std::filesystem::path> plugin_directories)
    : plugin_directories_(plugin_directories.empty() ? default_plugin_directories() : std::move(plugin_directories)) {}

Gr4RuntimeManager::~Gr4RuntimeManager() {
    std::lock_guard lock(mutex_);
    for (auto& [session_id, execution] : executions_) {
        if (!execution) {
            continue;
        }
        try {
            domain::Session session;
            session.id = session_id;
            stop_locked(session, *execution);
        } catch (...) {
        }
        static auto* retired = new std::vector<std::unique_ptr<Execution>>();
        retired->push_back(std::move(execution));
    }
    executions_.clear();
}

std::vector<std::filesystem::path> Gr4RuntimeManager::default_plugin_directories() {
    if (const char* env = std::getenv("GNURADIO4_PLUGIN_DIRECTORIES"); env != nullptr && *env != '\0') {
        return split_plugin_directories(env);
    }
#if defined(GR4CP_DEFAULT_GNURADIO4_PLUGIN_DIR)
    return {std::filesystem::path(GR4CP_DEFAULT_GNURADIO4_PLUGIN_DIR)};
#else
    return {};
#endif
}

void Gr4RuntimeManager::prepare(const domain::Session& session) {
    std::lock_guard lock(mutex_);
    (void)prepare_locked(session);
}

void Gr4RuntimeManager::start(const domain::Session& session) {
    std::lock_guard lock(mutex_);
    auto& execution = prepare_locked(session);

    if (execution.running) {
        return;
    }

    if (execution.scheduler == nullptr) {
        throw runtime_error(session, "start", "scheduler is unavailable");
    }

    execution.scheduler->start();
    execution.running = true;
}

void Gr4RuntimeManager::stop(const domain::Session& session) {
    std::lock_guard lock(mutex_);
    auto* execution = find_execution_locked(session.id);
    if (execution == nullptr) {
        return;
    }
    stop_locked(session, *execution);
}

void Gr4RuntimeManager::destroy(const domain::Session& session) {
    std::lock_guard lock(mutex_);
    destroy_locked(session);
}

void Gr4RuntimeManager::set_block_settings(const domain::Session& session,
                                           const std::string& block_target,
                                           const gr::property_map& patch,
                                           BlockSettingsMode mode) {
    std::lock_guard lock(mutex_);
    auto* execution = find_execution_locked(session.id);
    if (execution == nullptr || !execution->running) {
        throw runtime_error(session, "settings update", "session runtime is not running");
    }

    auto* scheduler_block = execution->scheduler->asBlockModel();
    if (scheduler_block == nullptr) {
        throw runtime_error(session, "settings update", "scheduler block is unavailable");
    }

    const auto resolved_block = resolve_runtime_block(*scheduler_block, block_target);
    if (!resolved_block) {
        throw make_block_not_found_error(*scheduler_block, block_target);
    }
    const auto runtime_block_id = std::string(resolved_block->uniqueName());

    const auto* endpoint = mode == BlockSettingsMode::Staged ? gr::block::property::kStagedSetting
                                                             : gr::block::property::kSetting;
    try {
        const auto reply = send_block_settings_request<gr::message::Command::Set>(
            *scheduler_block,
            runtime_block_id,
            endpoint,
            patch,
            std::chrono::milliseconds(1000));
        (void)parse_settings_reply(reply, runtime_block_id, endpoint);
    } catch (const BlockNotFoundError&) {
        throw;
    } catch (const ReplyTimeoutError&) {
        throw;
    } catch (const std::exception& error) {
        throw runtime_error(session, "settings update", error.what());
    }
}

gr::property_map Gr4RuntimeManager::get_block_settings(const domain::Session& session, const std::string& block_target) {
    std::lock_guard lock(mutex_);
    auto* execution = find_execution_locked(session.id);
    if (execution == nullptr || !execution->running) {
        throw runtime_error(session, "settings read", "session runtime is not running");
    }

    auto* scheduler_block = execution->scheduler->asBlockModel();
    if (scheduler_block == nullptr) {
        throw runtime_error(session, "settings read", "scheduler block is unavailable");
    }

    const auto resolved_block = resolve_runtime_block(*scheduler_block, block_target);
    if (!resolved_block) {
        throw make_block_not_found_error(*scheduler_block, block_target);
    }
    const auto runtime_block_id = std::string(resolved_block->uniqueName());

    try {
        const auto reply = send_block_settings_request<gr::message::Command::Get>(
            *scheduler_block,
            runtime_block_id,
            gr::block::property::kSetting,
            {},
            std::chrono::milliseconds(1000));
        return parse_settings_reply(reply, runtime_block_id, gr::block::property::kSetting);
    } catch (const BlockNotFoundError&) {
        throw;
    } catch (const ReplyTimeoutError&) {
        throw;
    } catch (const std::exception& error) {
        throw runtime_error(session, "settings read", error.what());
    }
}

Gr4RuntimeManager::Execution& Gr4RuntimeManager::prepare_locked(const domain::Session& session) {
    if (auto* existing = find_execution_locked(session.id); existing != nullptr) {
        return *existing;
    }

    ensure_runtime_environment(plugin_directories_);

    auto execution = std::make_unique<Execution>();
    try {
        auto graph = gr::loadGrc(gr::globalPluginLoader(), normalize_graph_content(session.grc_content));
        const auto scheduler_alias = session.scheduler_alias.value_or(default_scheduler_alias());
        auto scheduler = gr::globalPluginLoader().instantiateScheduler(scheduler_alias);
        if (!scheduler) {
            throw runtime_error(session, "prepare", std::format("failed to initialize scheduler: {}", scheduler_alias));
        }
        scheduler->setGraph(std::move(*graph));
        execution->scheduler = std::move(scheduler);
    } catch (const std::exception& error) {
        throw runtime_error(session, "prepare", error.what());
    }

    auto [it, inserted] = executions_.emplace(session.id, std::move(execution));
    return *it->second;
}

Gr4RuntimeManager::Execution* Gr4RuntimeManager::find_execution_locked(const std::string& session_id) {
    const auto it = executions_.find(session_id);
    return it == executions_.end() ? nullptr : it->second.get();
}

void Gr4RuntimeManager::stop_locked(const domain::Session& session, Execution& execution) {
    if (execution.running) {
        if (execution.scheduler == nullptr) {
            throw runtime_error(session, "stop", "scheduler is unavailable");
        }
        try {
            execution.scheduler->stop();
        } catch (const std::exception& error) {
            throw runtime_error(session, "stop", error.what());
        }
    }
    execution.running = false;
}

void Gr4RuntimeManager::destroy_locked(const domain::Session& session) {
    auto it = executions_.find(session.id);
    if (it == executions_.end()) {
        return;
    }

    auto execution = std::move(it->second);
    executions_.erase(it);
    if (!execution) {
        return;
    }

    stop_locked(session, *execution);
    static auto* retired = new std::vector<std::unique_ptr<Execution>>();
    retired->push_back(std::move(execution));
}

}  // namespace gr4cp::runtime
