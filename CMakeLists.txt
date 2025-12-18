cmake_minimum_required(VERSION 3.20)
project(DebianMirrorTester VERSION 0.0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# We use features added in curl 7.55.0 (August 2017)
# (CURLINFO_CONTENT_LENGTH_DOWNLOAD_T etc)
find_package(CURL 7.55.0 REQUIRED)

# ============================================
# Standard build
# ============================================
include_directories(${PROJECT_SOURCE_DIR}/include)
set(SOURCES
    src/main.cpp
    src/mirror_fetcher.cpp
    src/performance_tester.cpp
)
add_executable(dmt ${SOURCES})
target_link_libraries(dmt PRIVATE CURL::libcurl)
target_compile_options(dmt PRIVATE -O3)

# ============================================
# CPack configuration
# ============================================
option(WITH_CPACK "Enable CPack configuration for packaging" OFF)
if (WITH_CPACK)
    include(GNUInstallDirs)

    string(TIMESTAMP BUILD_DATE "%B %Y")
    configure_file(
        "${PROJECT_SOURCE_DIR}/dmt.1.in"
        "${PROJECT_BINARY_DIR}/dmt.1"
        @ONLY
    )

    install(TARGETS dmt
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(FILES "${PROJECT_BINARY_DIR}/dmt.1"
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
    )

    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_PACKAGE_NAME "dmt")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4 (>= 7.55.0), ca-certificates")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "sukibaby (https://github.com/sukibaby/dmt/)")
    set(CPACK_PACKAGE_CONTACT "https://github.com/sukibaby/dmt/")
    set(CPACK_PACKAGE_DESCRIPTION "Debian Mirror Tester - A tool to test and rank Debian mirrors based on download speed.")
    include(CPack)
endif()

