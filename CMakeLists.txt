cmake_minimum_required(VERSION 3.16)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

##
## PROJECT
## name and version
##
project(cachyos-pi
        VERSION 0.0.1
        LANGUAGES CXX)


##
## INCLUDE
##
include(GNUInstallDirs)
include(StandardProjectSettings)
include(CompilerWarnings)
include(EnableCcache)
include(Linker)
include(StaticAnalyzers)
include(Sanitizers)
include(CPM)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Qt5 COMPONENTS Widgets LinguistTools REQUIRED)

pkg_check_modules(
  LIBALPM
  REQUIRED
  IMPORTED_TARGET
  libalpm>=13.0.0)
CPMAddPackage(
  NAME fmt
  GITHUB_REPOSITORY fmtlib/fmt
  GIT_TAG 10.0.0
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  GIT_TAG 31cf79a70dfa61a4b2b7509eecca6a3b269bfc4a
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME cpr
  GITHUB_REPOSITORY libcpr/cpr
  GIT_TAG 1.10.4
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME rapidyaml
  GITHUB_REPOSITORY biojppm/rapidyaml
  GIT_TAG 0e864cb3f047efd7b57d2e8e4f2fc4cf94e53f06
  EXCLUDE_FROM_ALL YES
)
if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
   CPMAddPackage(
     NAME range-v3
     GITHUB_REPOSITORY ericniebler/range-v3
     GIT_TAG 4624c63972c6f2c2871c7b87813c42048ddb80ad
     EXCLUDE_FROM_ALL YES
   )
endif()

find_package(PolkitQt5-1 REQUIRED)

##
## CONFIGURATION
##
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=thin -fwhole-program-vtables")
   set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=thin -fwhole-program-vtables")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -fwhole-program -fuse-linker-plugin")
endif()

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_20)

# Files with translation sources. You should run lupdate manually when needed to regenerate them
file(GLOB TS_FILES "${PROJECT_SOURCE_DIR}/lang/*.ts")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lang)

set_source_files_properties(${TS_FILES} PROPERTIES OUTPUT_LOCATION "lang") # set qm files output directory
qt5_add_translation(QM_FILES ${TS_FILES})   # generate qm files

##
## Target
##
add_executable(${PROJECT_NAME}-bin
    ${RESOURCES} ${QM_FILES} # note that ${QM_FILES} should be included as sources to be generated.
    images.qrc
    src/ini.hpp
    src/utils.hpp src/utils.cpp
    src/lockfile.hpp src/lockfile.cpp
    src/versionnumber.hpp
    src/alpm_helper.hpp src/alpm_helper.cpp
    src/pacmancache.hpp src/pacmancache.cpp
    src/remotes.hpp src/remotes.cpp
    src/about.hpp src/about.cpp
    src/cmd.hpp src/cmd.cpp
    src/config.hpp src/config.cpp
    src/mainwindow.hpp src/mainwindow.cpp
    src/mainwindow.ui
    src/main.cpp)

# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

# Add linker configuration
configure_linker(project_options)

# sanitizer options if supported by compiler
enable_sanitizers(project_options)

include_directories(${CMAKE_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME}-bin PRIVATE project_warnings project_options Qt5::Widgets Threads::Threads spdlog::spdlog fmt::fmt ryml::ryml cpr::cpr PkgConfig::LIBALPM)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
   target_link_libraries(${PROJECT_NAME}-bin PRIVATE range-v3::range-v3)
endif()

option(ENABLE_UNITY "Enable Unity builds of projects" OFF)
if(ENABLE_UNITY)
   # Add for any project you want to apply unity builds for
   set_target_properties(${PROJECT_NAME}-bin PROPERTIES UNITY_BUILD ON)
endif()

install(
   TARGETS ${PROJECT_NAME}-bin
   RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(
   PROGRAMS src/${PROJECT_NAME}
   DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
   FILES org.cachyos.cachyos-pi.pkexec.policy
   DESTINATION "${POLKITQT-1_POLICY_FILES_INSTALL_DIR}"
)

install(
   FILES "${PROJECT_SOURCE_DIR}/pkglist.yaml"
   DESTINATION lib/cachyos-pi
)
file(GLOB HELP_FILES "${PROJECT_SOURCE_DIR}/help/*")
install(
   FILES ${HELP_FILES}
   DESTINATION share/doc/cachyos-packageinstaller
)

install(
   FILES cachyos-pi.desktop
   DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(
   FILES icons/cachyos-pi.png
   DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
