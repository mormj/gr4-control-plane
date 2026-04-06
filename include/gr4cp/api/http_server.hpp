#pragma once

#include "gr4cp/app/block_settings_service.hpp"
#include "gr4cp/app/block_catalog_service.hpp"
#include "gr4cp/app/scheduler_catalog_service.hpp"
#include "gr4cp/app/session_service.hpp"

namespace httplib {
class Server;
}

namespace gr4cp::api {

void register_routes(httplib::Server& server,
                     app::SessionService& session_service,
                     app::BlockCatalogService& block_catalog_service,
                     app::BlockSettingsService& block_settings_service,
                     app::SchedulerCatalogService& scheduler_catalog_service);

}  // namespace gr4cp::api
