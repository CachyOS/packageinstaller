##
## Small qt-free lib/backend. For now mostly copy of GUCC code.
##
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

# header-only
add_library(pi_names INTERFACE include/pi/cctype_wrapper.hpp include/pi/names.hpp)
add_library(pi::names ALIAS pi_names)
target_include_directories(pi_names INTERFACE include)

add_library(pi STATIC
    include/pi/conf.hpp src/conf.cpp
    include/pi/error.hpp src/error.cpp
    include/pi/file_utils.hpp src/file_utils.cpp
    include/pi/pacman_conf.hpp src/pacman_conf.cpp
    include/pi/process.hpp src/process.cpp
    include/pi/string_utils.hpp src/string_utils.cpp)
add_library(pi::pi ALIAS pi)
target_include_directories(pi PUBLIC include)
target_include_directories(pi PRIVATE src)
target_link_libraries(pi PUBLIC pi::names fmt::fmt spdlog::spdlog PRIVATE project_options project_warnings)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
