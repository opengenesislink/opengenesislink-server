cmake_minimum_required(VERSION 3.25)
project(OpenGenesisLINK VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OGL_BUILD_TESTS "Build OpenGenesisLINK tests" ON)
option(OGL_ENABLE_WERROR "Treat warnings as errors" OFF)

find_package(Threads REQUIRED)

add_library(opengenesis-foundation
    src/common/log.cpp
    src/config/toml_config.cpp
    src/network/tcp.cpp
    src/protocol/frame.cpp
    src/core/world_registry.cpp
    src/core/region_registry.cpp
    src/core/node_sessions.cpp
    src/core/admin_http.cpp
    src/physics/physics_world.cpp
    src/world/region_runtime.cpp
)
add_library(OpenGenesis::Foundation ALIAS opengenesis-foundation)

target_include_directories(opengenesis-foundation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(opengenesis-foundation PUBLIC Threads::Threads)
target_compile_definitions(opengenesis-foundation PUBLIC OGL_VERSION="${PROJECT_VERSION}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(opengenesis-foundation PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(OGL_ENABLE_WERROR)
        target_compile_options(opengenesis-foundation PRIVATE -Werror)
    endif()
endif()

add_executable(opengenesis-core apps/core/main.cpp)
target_link_libraries(opengenesis-core PRIVATE OpenGenesis::Foundation)
add_executable(opengenesis-world apps/world/main.cpp)
target_link_libraries(opengenesis-world PRIVATE OpenGenesis::Foundation)

install(TARGETS opengenesis-foundation opengenesis-core opengenesis-world
    RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config/ DESTINATION share/opengenesislink/config)

include(CTest)
if(BUILD_TESTING AND OGL_BUILD_TESTS)
    add_executable(ogl-foundation-tests tests/foundation_tests.cpp)
    target_link_libraries(ogl-foundation-tests PRIVATE OpenGenesis::Foundation)
    add_test(NAME foundation COMMAND ogl-foundation-tests)
endif()
