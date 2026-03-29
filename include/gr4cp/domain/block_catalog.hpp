#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gr4cp::domain {

using BlockParameterDefault = std::variant<std::nullptr_t, bool, int, double, std::string>;

struct BlockPortDescriptor {
    std::string name;
    std::string type;
};

struct BlockParameterDescriptor {
    std::string name;
    std::string type;
    bool required{false};
    BlockParameterDefault default_value{nullptr};
    std::string summary;
    std::optional<std::string> runtime_mutability;
    std::optional<std::string> value_kind;
    std::vector<std::string> enum_options;
    std::map<std::string, std::string> enum_labels;
    std::optional<std::string> enum_source;
    std::optional<std::string> ui_hint;
    std::optional<bool> allow_custom_value;

    BlockParameterDescriptor() = default;

    BlockParameterDescriptor(std::string name_,
                             std::string type_,
                             bool required_,
                             BlockParameterDefault default_value_,
                             std::string summary_)
        : name(std::move(name_)),
          type(std::move(type_)),
          required(required_),
          default_value(std::move(default_value_)),
          summary(std::move(summary_)) {}
};

struct BlockDescriptor {
    std::string id;
    std::string name;
    std::string category;
    std::string summary;
    std::vector<BlockPortDescriptor> inputs;
    std::vector<BlockPortDescriptor> outputs;
    std::vector<BlockParameterDescriptor> parameters;
};

bool operator==(const BlockPortDescriptor& left, const BlockPortDescriptor& right);
bool operator==(const BlockParameterDescriptor& left, const BlockParameterDescriptor& right);
bool operator==(const BlockDescriptor& left, const BlockDescriptor& right);

}  // namespace gr4cp::domain
