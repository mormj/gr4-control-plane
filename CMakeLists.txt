cmake_minimum_required(VERSION 3.25)

project(gr4_control_plane_mvp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

option(USE_CCACHE "Use ccache if available" ON)

option(GR4CP_ENABLE_GR4_CATALOG "Enable the required GNU Radio 4-backed block catalog provider" ON)

set(_gr4cp_default_gnuradio4_prefix "")
if(DEFINED ENV{GR4CP_GNURADIO4_PREFIX})
    set(_gr4cp_default_gnuradio4_prefix "$ENV{GR4CP_GNURADIO4_PREFIX}")
endif()
set(GR4CP_GNURADIO4_PREFIX "${_gr4cp_default_gnuradio4_prefix}" CACHE PATH "Optional prefix containing gnuradio4Config.cmake; leave empty to use CMAKE_PREFIX_PATH/default package discovery")

# Use ccache if found and enabled
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND USE_CCACHE)
    message(STATUS "ccache found and will be used")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
else()
    message(STATUS "ccache will not be used")
endif()

# Prefer forced colored compiler output if there's a chance we'd otherwise get none at all. Ninja and ccache
# can consume compiler output and break compiler terminal color auto-detection.
if((CMAKE_GENERATOR STREQUAL "Ninja" OR (CCACHE_PROGRAM AND USE_CCACHE)) AND NOT DEFINED CMAKE_COLOR_DIAGNOSTICS)
    message(
        STATUS
        "Forcing compiler color output due to the use of Ninja and/or ccache. Use -DCMAKE_COLOR_DIAGNOSTICS=OFF to turn it off."
    )
    set(CMAKE_COLOR_DIAGNOSTICS ON)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CPPHTTPLIB_OPENSSL REQUIRED IMPORTED_TARGET openssl)
pkg_check_modules(CPPHTTPLIB_BROTLICOMMON REQUIRED IMPORTED_TARGET libbrotlicommon)
pkg_check_modules(CPPHTTPLIB_BROTLIDEC REQUIRED IMPORTED_TARGET libbrotlidec)
pkg_check_modules(CPPHTTPLIB_BROTLIENC REQUIRED IMPORTED_TARGET libbrotlienc)

find_package(httplib CONFIG QUIET)
if(httplib_FOUND)
    set(GR4CP_HTTPLIB_TARGET httplib::httplib)
else()
    find_path(GR4CP_CPPHTTPLIB_INCLUDE_DIR
        NAMES httplib.h
        PATH_SUFFIXES include
    )

    if(NOT GR4CP_CPPHTTPLIB_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find cpp-httplib. Install it with a CMake package config or ensure httplib.h is visible to CMake.")
    endif()

    add_library(gr4cp_cpp_httplib INTERFACE)
    add_library(gr4cp::cpp_httplib ALIAS gr4cp_cpp_httplib)
    target_include_directories(gr4cp_cpp_httplib INTERFACE "${GR4CP_CPPHTTPLIB_INCLUDE_DIR}")
    set(GR4CP_HTTPLIB_TARGET gr4cp::cpp_httplib)
endif()

set(GR4CP_HAVE_GNURADIO4 FALSE)
if(GR4CP_ENABLE_GR4_CATALOG)
    set(_gr4cp_saved_prefix_path "${CMAKE_PREFIX_PATH}")
    if(GR4CP_GNURADIO4_PREFIX)
        list(PREPEND CMAKE_PREFIX_PATH "${GR4CP_GNURADIO4_PREFIX}")
    endif()
    find_package(gnuradio4 CONFIG QUIET)
    set(CMAKE_PREFIX_PATH "${_gr4cp_saved_prefix_path}")
    if(gnuradio4_FOUND)
        set(GR4CP_HAVE_GNURADIO4 TRUE)
        cmake_path(GET gnuradio4_DIR PARENT_PATH _gr4cp_gnuradio4_cmake_dir)
        cmake_path(GET _gr4cp_gnuradio4_cmake_dir PARENT_PATH GR4CP_GNURADIO4_LIB_DIR)
        message(STATUS "Building with GNU Radio 4-backed block catalog from ${GR4CP_GNURADIO4_LIB_DIR}")
    else()
        message(FATAL_ERROR "GNU Radio 4 is required for the block catalog. Set GR4CP_GNURADIO4_PREFIX to a working gnuradio4 installation.")
    endif()
else()
    message(WARNING "GR4CP_ENABLE_GR4_CATALOG=OFF disables the production server catalog path; tests may still use local fake providers.")
endif()

add_library(gr4cp_core
    src/app/block_settings_service.cpp
    src/app/block_catalog_service.cpp
    src/api/http_server.cpp
    src/catalog/static_block_catalog_provider.cpp
    src/domain/block_catalog.cpp
    src/domain/session.cpp
    src/app/session_service.cpp
    src/runtime/gr4_runtime_manager.cpp
    src/runtime/stub_runtime_manager.cpp
    src/storage/in_memory_session_repository.cpp
)

if(GR4CP_HAVE_GNURADIO4)
    target_sources(gr4cp_core
        PRIVATE
            src/catalog/gr4_block_catalog_provider.cpp
    )
    target_compile_definitions(gr4cp_core
        PUBLIC
            GR4CP_HAVE_GNURADIO4
            GR4CP_DEFAULT_GNURADIO4_PLUGIN_DIR="${GR4CP_GNURADIO4_LIB_DIR}"
    )
    target_link_libraries(gr4cp_core
        PUBLIC
            gnuradio4::gnuradio-core
            gnuradio4::gnuradio-blocklib-core
    )
    set(_gr4cp_test_env
        "GNURADIO4_PLUGIN_DIRECTORIES=${GR4CP_GNURADIO4_LIB_DIR}"
        "LD_LIBRARY_PATH=${GR4CP_GNURADIO4_LIB_DIR}"
    )
endif()

target_include_directories(gr4cp_core
    PUBLIC
        include
)

target_link_libraries(gr4cp_core
    PUBLIC
        ${GR4CP_HTTPLIB_TARGET}
        PkgConfig::CPPHTTPLIB_OPENSSL
        PkgConfig::CPPHTTPLIB_BROTLICOMMON
        PkgConfig::CPPHTTPLIB_BROTLIDEC
        PkgConfig::CPPHTTPLIB_BROTLIENC
        ZLIB::ZLIB
        nlohmann_json::nlohmann_json
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(gr4cp_core
        PUBLIC
            stdc++exp
    )
endif()

add_library(gr4cp_cli
    src/cli/cli.cpp
)

target_include_directories(gr4cp_cli
    PUBLIC
        include
)

target_link_libraries(gr4cp_cli
    PUBLIC
        ${GR4CP_HTTPLIB_TARGET}
        PkgConfig::CPPHTTPLIB_OPENSSL
        PkgConfig::CPPHTTPLIB_BROTLICOMMON
        PkgConfig::CPPHTTPLIB_BROTLIDEC
        PkgConfig::CPPHTTPLIB_BROTLIENC
        ZLIB::ZLIB
        nlohmann_json::nlohmann_json
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(gr4cp_cli
        PUBLIC
            stdc++exp
    )
endif()

add_executable(gr4cp_server
    src/main.cpp
)

target_link_libraries(gr4cp_server
    PRIVATE
        gr4cp_core
)

add_executable(gr4cp_cli_exec
    src/cli_main.cpp
)

set_target_properties(gr4cp_cli_exec PROPERTIES OUTPUT_NAME gr4cp-cli)

target_link_libraries(gr4cp_cli_exec
    PRIVATE
        gr4cp_cli
)

enable_testing()

add_executable(gr4cp_smoke_test
    test/healthz_smoke_test.cpp
)

target_link_libraries(gr4cp_smoke_test
    PRIVATE
        gr4cp_core
        GTest::gtest_main
        Threads::Threads
)

include(GoogleTest)
if(GR4CP_HAVE_GNURADIO4)
    gtest_discover_tests(gr4cp_smoke_test PROPERTIES ENVIRONMENT "${_gr4cp_test_env}")
else()
    gtest_discover_tests(gr4cp_smoke_test)
endif()

add_executable(gr4cp_core_test
    test/block_settings_service_test.cpp
    test/session_service_test.cpp
    test/block_catalog_service_test.cpp
    test/gr4_block_catalog_provider_test.cpp
    test/gr4_runtime_manager_test.cpp
)

target_link_libraries(gr4cp_core_test
    PRIVATE
        gr4cp_core
        GTest::gtest_main
        Threads::Threads
)

if(GR4CP_HAVE_GNURADIO4)
    gtest_discover_tests(gr4cp_core_test PROPERTIES ENVIRONMENT "${_gr4cp_test_env}")
else()
    gtest_discover_tests(gr4cp_core_test)
endif()

add_executable(gr4cp_http_test
    test/http_api_test.cpp
)

target_link_libraries(gr4cp_http_test
    PRIVATE
        gr4cp_core
        GTest::gtest_main
        Threads::Threads
)

if(GR4CP_HAVE_GNURADIO4)
    gtest_discover_tests(gr4cp_http_test PROPERTIES ENVIRONMENT "${_gr4cp_test_env}")
else()
    gtest_discover_tests(gr4cp_http_test)
endif()

add_executable(gr4cp_cli_test
    test/cli_test.cpp
)

target_link_libraries(gr4cp_cli_test
    PRIVATE
        gr4cp_cli
        gr4cp_core
        GTest::gtest_main
        Threads::Threads
)

target_compile_definitions(gr4cp_cli_test
    PRIVATE
        GR4CP_CLI_EXECUTABLE="$<TARGET_FILE:gr4cp_cli_exec>"
)

if(GR4CP_HAVE_GNURADIO4)
    gtest_discover_tests(gr4cp_cli_test PROPERTIES ENVIRONMENT "${_gr4cp_test_env}")
else()
    gtest_discover_tests(gr4cp_cli_test)
endif()

install(TARGETS gr4cp_server gr4cp_cli_exec
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/gr4cp/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gr4cp
    FILES_MATCHING PATTERN "*.hpp"
)
