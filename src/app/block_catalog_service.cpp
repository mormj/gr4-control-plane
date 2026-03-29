#include "gr4cp/app/block_catalog_service.hpp"

#include <algorithm>

#include "gr4cp/app/session_service.hpp"

namespace gr4cp::app {

BlockCatalogService::BlockCatalogService(const catalog::BlockCatalogProvider& provider) : provider_(provider) {}

std::vector<domain::BlockDescriptor> BlockCatalogService::list() const {
    std::lock_guard lock(mutex_);
    return cached_blocks();
}

domain::BlockDescriptor BlockCatalogService::get(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto& blocks = cached_blocks();
    const auto it = std::find_if(blocks.begin(), blocks.end(), [&id](const domain::BlockDescriptor& block) {
        return block.id == id;
    });
    if (it == blocks.end()) {
        throw NotFoundError("block not found: " + id);
    }
    return *it;
}

const std::vector<domain::BlockDescriptor>& BlockCatalogService::cached_blocks() const {
    if (!cached_blocks_.has_value()) {
        auto blocks = provider_.list();
        std::sort(blocks.begin(), blocks.end(), [](const domain::BlockDescriptor& left, const domain::BlockDescriptor& right) {
            if (left.category != right.category) {
                return left.category < right.category;
            }
            if (left.name != right.name) {
                return left.name < right.name;
            }
            return left.id < right.id;
        });
        cached_blocks_ = std::move(blocks);
    }
    return *cached_blocks_;
}

}  // namespace gr4cp::app
