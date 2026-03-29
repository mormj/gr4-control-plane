#pragma once

#include "gr4cp/catalog/block_catalog_provider.hpp"

namespace gr4cp::catalog {

class StaticBlockCatalogProvider final : public BlockCatalogProvider {
public:
    std::vector<domain::BlockDescriptor> list() const override;
};

}  // namespace gr4cp::catalog
