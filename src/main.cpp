#include "gr4cp/app/block_catalog_service.hpp"
#include "gr4cp/app/block_settings_service.hpp"
#include "gr4cp/app/session_service.hpp"
#include "gr4cp/api/http_server.hpp"
#include "gr4cp/catalog/gr4_block_catalog_provider.hpp"
#include "gr4cp/runtime/gr4_runtime_manager.hpp"
#include "gr4cp/storage/in_memory_session_repository.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#include <httplib.h>

int main() {
#if !defined(GR4CP_HAVE_GNURADIO4)
    std::cerr << "GNU Radio 4 catalog support is not built. Configure with GR4CP_ENABLE_GR4_CATALOG=ON "
                 "and a working gnuradio4 installation.\n";
    return 1;
#else
    gr4cp::storage::InMemorySessionRepository repository;
    gr4cp::runtime::Gr4RuntimeManager runtime_manager;
    gr4cp::app::SessionService session_service(repository, runtime_manager);
    gr4cp::app::BlockSettingsService block_settings_service(repository, runtime_manager);
    gr4cp::catalog::Gr4BlockCatalogProvider block_catalog_provider;
    gr4cp::app::BlockCatalogService block_catalog_service(block_catalog_provider);

    try {
        (void)block_catalog_service.list();
        std::cout << "Using GNU Radio 4-backed block catalog\n";
    } catch (const gr4cp::catalog::CatalogLoadError& error) {
        std::cerr << "GNU Radio 4 catalog initialization failed: " << error.what() << '\n';
        return 1;
    }

    httplib::Server server;
    gr4cp::api::register_routes(server, session_service, block_catalog_service, block_settings_service);

    const char* port_env = std::getenv("GR4CP_PORT");
    const int port = port_env != nullptr ? std::stoi(port_env) : 8080;

    std::cout << "Listening on 0.0.0.0:" << port << '\n';
    server.listen("0.0.0.0", port);
    return 0;
#endif
}
