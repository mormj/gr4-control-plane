#pragma once

#include <stdexcept>
#include <vector>

#include "gr4cp/domain/block_catalog.hpp"

namespace gr4cp::catalog {

class CatalogLoadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class BlockCatalogProvider {
public:
    virtual ~BlockCatalogProvider() = default;

    virtual std::vector<domain::BlockDescriptor> list() const = 0;
};

}  // namespace gr4cp::catalog
