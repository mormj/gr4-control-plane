#include "gr4cp/domain/block_catalog.hpp"

namespace gr4cp::domain {

bool operator==(const BlockPortDescriptor& left, const BlockPortDescriptor& right) {
    return left.name == right.name && left.type == right.type && left.cardinality_kind == right.cardinality_kind &&
           left.current_port_count == right.current_port_count && left.min_port_count == right.min_port_count &&
           left.render_port_count == right.render_port_count && left.max_port_count == right.max_port_count &&
           left.size_parameter == right.size_parameter &&
           left.handle_name_template == right.handle_name_template;
}

bool operator==(const BlockParameterDescriptor& left, const BlockParameterDescriptor& right) {
    return left.name == right.name && left.type == right.type && left.required == right.required &&
           left.default_value == right.default_value && left.summary == right.summary &&
           left.runtime_mutability == right.runtime_mutability && left.value_kind == right.value_kind &&
           left.enum_choices == right.enum_choices && left.enum_type == right.enum_type &&
           left.enum_labels == right.enum_labels && left.enum_source == right.enum_source &&
           left.ui_hint == right.ui_hint && left.allow_custom_value == right.allow_custom_value;
}

bool operator==(const BlockDescriptor& left, const BlockDescriptor& right) {
    return left.id == right.id && left.canonical_type == right.canonical_type && left.name == right.name && left.category == right.category &&
           left.summary == right.summary && left.inputs == right.inputs && left.outputs == right.outputs &&
           left.parameters == right.parameters;
}

}  // namespace gr4cp::domain
