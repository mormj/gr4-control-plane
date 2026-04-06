#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "gr4cp/catalog/scheduler_catalog_provider.hpp"
#include "gr4cp/domain/scheduler_catalog.hpp"

namespace gr4cp::app {

class SchedulerCatalogService {
public:
    explicit SchedulerCatalogService(const catalog::SchedulerCatalogProvider& provider);

    std::vector<domain::SchedulerDescriptor> list() const;
    domain::SchedulerDescriptor get(const std::string& id) const;

private:
    struct CachedCatalog {
        std::vector<domain::SchedulerDescriptor> schedulers;
        std::unordered_map<std::string, domain::SchedulerDescriptor> exact_schedulers;
    };

    const CachedCatalog& cached_catalog() const;

    const catalog::SchedulerCatalogProvider& provider_;
    mutable std::mutex mutex_;
    mutable std::optional<CachedCatalog> cached_catalog_;
};

}  // namespace gr4cp::app
