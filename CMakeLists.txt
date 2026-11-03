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
