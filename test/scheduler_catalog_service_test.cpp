#include "gr4cp/app/scheduler_catalog_service.hpp"

#include <gtest/gtest.h>

#include "gr4cp/app/session_service.hpp"
#include "gr4cp/catalog/scheduler_catalog_provider.hpp"

namespace {

class TestSchedulerCatalogProvider final : public gr4cp::catalog::SchedulerCatalogProvider {
public:
    std::vector<gr4cp::domain::SchedulerDescriptor> list() const override {
        return {
            {"gr::scheduler::SimpleMulti"},
            {"gr::scheduler::SimpleSingle"},
        };
    }
};

TEST(SchedulerCatalogServiceTest, ListReturnsSortedSchedulerAliases) {
    TestSchedulerCatalogProvider provider;
    gr4cp::app::SchedulerCatalogService service{provider};

    const auto schedulers = service.list();

    ASSERT_EQ(schedulers.size(), 2U);
    EXPECT_EQ(schedulers[0].id, "gr::scheduler::SimpleMulti");
    EXPECT_EQ(schedulers[1].id, "gr::scheduler::SimpleSingle");
}

TEST(SchedulerCatalogServiceTest, GetReturnsSchedulerByAlias) {
    TestSchedulerCatalogProvider provider;
    gr4cp::app::SchedulerCatalogService service{provider};

    const auto scheduler = service.get("gr::scheduler::SimpleSingle");

    EXPECT_EQ(scheduler.id, "gr::scheduler::SimpleSingle");
}

TEST(SchedulerCatalogServiceTest, GetMissingSchedulerFails) {
    TestSchedulerCatalogProvider provider;
    gr4cp::app::SchedulerCatalogService service{provider};

    EXPECT_THROW(service.get("missing"), gr4cp::app::NotFoundError);
}

}  // namespace
