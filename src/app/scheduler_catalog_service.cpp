#include "gr4cp/app/scheduler_catalog_service.hpp"

#include <algorithm>
#include <utility>

#include "gr4cp/app/session_service.hpp"

namespace gr4cp::app {

SchedulerCatalogService::SchedulerCatalogService(const catalog::SchedulerCatalogProvider& provider) : provider_(provider) {}

std::vector<domain::SchedulerDescriptor> SchedulerCatalogService::list() const {
    std::lock_guard lock(mutex_);
    return cached_catalog().schedulers;
}

domain::SchedulerDescriptor SchedulerCatalogService::get(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto& catalog = cached_catalog();
    const auto it = catalog.exact_schedulers.find(id);
    if (it == catalog.exact_schedulers.end()) {
        throw NotFoundError("scheduler not found: " + id);
    }
    return it->second;
}

const SchedulerCatalogService::CachedCatalog& SchedulerCatalogService::cached_catalog() const {
    if (!cached_catalog_.has_value()) {
        auto schedulers = provider_.list();
        std::sort(schedulers.begin(), schedulers.end(), [](const domain::SchedulerDescriptor& left, const domain::SchedulerDescriptor& right) {
            return left.id < right.id;
        });

        CachedCatalog catalog;
        for (const auto& scheduler : schedulers) {
            catalog.exact_schedulers.insert_or_assign(scheduler.id, scheduler);
            catalog.schedulers.push_back(scheduler);
        }

        cached_catalog_ = std::move(catalog);
    }
    return *cached_catalog_;
}

}  // namespace gr4cp::app
