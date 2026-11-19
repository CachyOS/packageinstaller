CPMAddPackage(
    NAME doctest
    GITHUB_REPOSITORY doctest/doctest
    GIT_TAG v2.5.3
    EXCLUDE_FROM_ALL YES)

# doctest main is built once and shared by every test executable
add_library(pi_doctest_main OBJECT unit.cpp)
target_link_libraries(pi_doctest_main PRIVATE project_options doctest::doctest)

file(GLOB files CONFIGURE_DEPENDS unit-*.cpp)

foreach(file ${files})
    get_filename_component(file_basename ${file} NAME_WE)
    string(REGEX REPLACE "unit-([^$]+)" "\\1" name ${file_basename})

    add_executable(test-pi-${name} $<TARGET_OBJECTS:pi_doctest_main> ${file})
    target_compile_definitions(test-pi-${name} PRIVATE DOCTEST_CONFIG_SUPER_FAST_ASSERTS)
    target_link_libraries(test-pi-${name} PRIVATE project_warnings project_options pi::pi doctest::doctest)

    add_test(NAME pi-${name} COMMAND test-pi-${name} --no-skip)
endforeach()
