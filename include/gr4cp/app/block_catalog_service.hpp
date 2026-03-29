#pragma once

#include <mutex>
#include <optional>
#include <vector>

#include "gr4cp/catalog/block_catalog_provider.hpp"
#include "gr4cp/domain/block_catalog.hpp"

namespace gr4cp::app {

class BlockCatalogService {
public:
    explicit BlockCatalogService(const catalog::BlockCatalogProvider& provider);

    std::vector<domain::BlockDescriptor> list() const;
    domain::BlockDescriptor get(const std::string& id) const;

private:
    const std::vector<domain::BlockDescriptor>& cached_blocks() const;

    const catalog::BlockCatalogProvider& provider_;
    mutable std::mutex mutex_;
    mutable std::optional<std::vector<domain::BlockDescriptor>> cached_blocks_;
};

}  // namespace gr4cp::app
