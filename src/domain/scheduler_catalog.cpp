#include "gr4cp/domain/scheduler_catalog.hpp"

namespace gr4cp::domain {

bool operator==(const SchedulerDescriptor& left, const SchedulerDescriptor& right) {
    return left.id == right.id;
}

}  // namespace gr4cp::domain
