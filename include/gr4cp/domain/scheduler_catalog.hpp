#pragma once

#include <string>
#include <utility>

namespace gr4cp::domain {

struct SchedulerDescriptor {
    std::string id;

    SchedulerDescriptor() = default;
    SchedulerDescriptor(std::string id_) : id(std::move(id_)) {}
};

bool operator==(const SchedulerDescriptor& left, const SchedulerDescriptor& right);

}  // namespace gr4cp::domain
