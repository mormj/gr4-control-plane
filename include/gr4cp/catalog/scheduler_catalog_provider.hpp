#pragma once

#include <stdexcept>
#include <vector>

#include "gr4cp/domain/scheduler_catalog.hpp"

namespace gr4cp::catalog {

class SchedulerCatalogLoadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class SchedulerCatalogProvider {
public:
    virtual ~SchedulerCatalogProvider() = default;

    virtual std::vector<domain::SchedulerDescriptor> list() const = 0;
};

}  // namespace gr4cp::catalog
