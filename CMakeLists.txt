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

add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_23)
configure_linker(project_options)
enable_sanitizers(project_options)

add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

set(PI_HELPER_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/cachyos-pi")

##
## Privileged helper: the only thing that runs as root (through pkexec).
##
add_executable(cachyos-pi-helper src/helper/main.cpp)
target_link_libraries(cachyos-pi-helper PRIVATE project_options project_warnings)

##
## Install
##
configure_file(org.cachyos.cachyos-pi.policy.in org.cachyos.cachyos-pi.policy @ONLY)

install(TARGETS cachyos-pi-helper RUNTIME DESTINATION ${PI_HELPER_DIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.cachyos.cachyos-pi.policy"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
