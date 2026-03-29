#include "gr4cp/app/block_catalog_service.hpp"

#include <gtest/gtest.h>

#include "gr4cp/app/session_service.hpp"
#include "gr4cp/catalog/block_catalog_provider.hpp"

namespace {

class TestBlockCatalogProvider final : public gr4cp::catalog::BlockCatalogProvider {
public:
    std::vector<gr4cp::domain::BlockDescriptor> list() const override {
        return {
            {
                .id = "blocks.sources.signal_source_f",
                .name = "Signal Source",
                .category = "Sources",
                .summary = "",
                .inputs = {},
                .outputs = {{"out", "float"}},
                .parameters = {{"frequency", "float", false, 1000.0, ""}},
            },
            {
                .id = "blocks.math.add_ff",
                .name = "Add",
                .category = "Math",
                .summary = "",
                .inputs = {{"in0", "float"}, {"in1", "float"}},
                .outputs = {{"out", "float"}},
                .parameters = {{"scale", "float", false, 1.0, ""}},
            },
            {
                .id = "blocks.analog.wfm_rcv",
                .name = "WFM Receive",
                .category = "Analog",
                .summary = "",
                .inputs = {{"in", "complex"}},
                .outputs = {{"out", "float"}},
                .parameters = {},
            },
        };
    }
};

class BlockCatalogServiceTest : public ::testing::Test {
protected:
    TestBlockCatalogProvider provider;
    gr4cp::app::BlockCatalogService service{provider};
};

TEST_F(BlockCatalogServiceTest, ListReturnsDeterministicOrdering) {
    const auto blocks = service.list();

    ASSERT_EQ(blocks.size(), 3U);
    EXPECT_EQ(blocks.front().category, "Analog");
    EXPECT_EQ(blocks.front().name, "WFM Receive");
    EXPECT_EQ(blocks.back().category, "Sources");
    EXPECT_EQ(blocks.back().name, "Signal Source");
}

TEST_F(BlockCatalogServiceTest, GetReturnsBlockById) {
    const auto block = service.get("blocks.math.add_ff");

    EXPECT_EQ(block.name, "Add");
    EXPECT_EQ(block.category, "Math");
    ASSERT_EQ(block.inputs.size(), 2U);
    EXPECT_EQ(block.inputs[0].name, "in0");
    ASSERT_EQ(block.parameters.size(), 1U);
    EXPECT_EQ(std::get<double>(block.parameters[0].default_value), 1.0);
}

TEST_F(BlockCatalogServiceTest, GetMissingBlockFails) {
    EXPECT_THROW(service.get("blocks.missing"), gr4cp::app::NotFoundError);
}

}  // namespace
