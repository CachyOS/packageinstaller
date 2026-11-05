cmake_minimum_required(VERSION 3.21)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

project(cachyos-pi
        VERSION 2.0.0
        LANGUAGES CXX)

include(GNUInstallDirs)
include(StandardProjectSettings)
include(CompilerWarnings)
include(EnableCcache)
include(Linker)
include(Sanitizers)

find_package(PkgConfig REQUIRED)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Network Concurrent)
find_package(AppStreamQt 1.0 REQUIRED)
pkg_check_modules(LIBALPM REQUIRED IMPORTED_TARGET libalpm>=15.0.0)

qt_standard_project_setup(REQUIRES 6.8)

add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_23)
configure_linker(project_options)
enable_sanitizers(project_options)

add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

set(PI_HELPER_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/cachyos-pi")

##
## GUI
##
qt_add_executable(${PROJECT_NAME}
    src/main.cpp
    src/alpmdb.hpp src/alpmdb.cpp
    src/catalog.hpp src/catalog.cpp
    src/packagemodel.hpp src/packagemodel.cpp
    src/aurclient.hpp src/aurclient.cpp
    src/iconprovider.hpp src/iconprovider.cpp
    src/transaction.hpp src/transaction.cpp
    src/backend.hpp src/backend.cpp)

qt_add_qml_module(${PROJECT_NAME}
    URI CachyOS.PackageInstaller
    VERSION 1.0
    RESOURCES
        pkglist.yaml
        icons/cachyos-pi.png)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PI_VERSION="${PROJECT_VERSION}"
    PI_HELPER_PATH="${PI_HELPER_DIR}/cachyos-pi-helper")
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE
    project_options project_warnings
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Concurrent
    AppStreamQt PkgConfig::LIBALPM)

##
## Privileged helper: the only thing that runs as root (through pkexec).
##
add_executable(cachyos-pi-helper src/helper/main.cpp)
target_link_libraries(cachyos-pi-helper PRIVATE project_options project_warnings)

##
## Install
##
configure_file(org.cachyos.cachyos-pi.policy.in org.cachyos.cachyos-pi.policy @ONLY)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS cachyos-pi-helper RUNTIME DESTINATION ${PI_HELPER_DIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.cachyos.cachyos-pi.policy"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES cachyos-pi.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES icons/cachyos-pi.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
