cmake_minimum_required(VERSION 3.16)
project(cxd VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cxd src/cxd.cpp)
target_include_directories(cxd PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

option(CXD_BUILD_TESTS "Build cxd test suite" OFF)
if(CXD_BUILD_TESTS)
    add_executable(cxd_tests tests/test_cxd.cpp)
    target_link_libraries(cxd_tests PRIVATE cxd)
    target_compile_options(cxd_tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )
    add_test(NAME cxd_tests COMMAND cxd_tests)
endif()

install(TARGETS cxd EXPORT cxdTargets)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT cxdTargets
    FILE cxdTargets.cmake
    NAMESPACE cxd::
    DESTINATION lib/cmake/cxd
)
