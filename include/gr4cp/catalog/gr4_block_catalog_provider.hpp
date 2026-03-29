#pragma once

#include <filesystem>
#include <vector>

#include "gr4cp/catalog/block_catalog_provider.hpp"

namespace gr4cp::catalog {

class Gr4BlockCatalogProvider final : public BlockCatalogProvider {
public:
    explicit Gr4BlockCatalogProvider(std::vector<std::filesystem::path> plugin_directories = {});

    std::vector<domain::BlockDescriptor> list() const override;

private:
    std::vector<std::filesystem::path> plugin_directories_;
};

}  // namespace gr4cp::catalog
