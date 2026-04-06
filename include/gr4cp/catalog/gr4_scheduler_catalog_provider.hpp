#pragma once

#include <filesystem>
#include <vector>

#include "gr4cp/catalog/scheduler_catalog_provider.hpp"

namespace gr4cp::catalog {

class Gr4SchedulerCatalogProvider final : public SchedulerCatalogProvider {
public:
    explicit Gr4SchedulerCatalogProvider(std::vector<std::filesystem::path> plugin_directories = {});

    std::vector<domain::SchedulerDescriptor> list() const override;

private:
    std::vector<std::filesystem::path> plugin_directories_;
};

}  // namespace gr4cp::catalog
