cmake_minimum_required(VERSION 3.25)
project(OpenGenesisLINK VERSION 3.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OGL_BUILD_TESTS "Build OpenGenesisLINK tests" ON)
option(OGL_ENABLE_WERROR "Treat warnings as errors" OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(opengenesis-foundation
    src/common/log.cpp
    src/config/toml_config.cpp
    src/security/crypto.cpp
    src/security/scene_ticket.cpp
    src/security/rate_limiter.cpp
    src/platform/socket.cpp
    src/platform/filesystem.cpp
    src/network/tcp.cpp
    src/protocol/frame.cpp
    src/core/world_registry.cpp
    src/core/region_registry.cpp
    src/core/node_sessions.cpp
    src/core/admin_http.cpp
    src/core/identity_store.cpp
    src/core/session_store.cpp
    src/core/asset_store.cpp
    src/core/inventory_store.cpp
    src/core/presence_store.cpp
    src/core/friends_store.cpp
    src/core/message_store.cpp
    src/core/group_store.cpp
    src/core/parcel_store.cpp
    src/core/moderation_store.cpp
    src/core/audit_store.cpp
    src/core/estate_store.cpp
    src/core/landmark_store.cpp
    src/core/notification_store.cpp
    src/core/group_channel_store.cpp
    src/avatar/appearance_store.cpp
    src/core/crossing_store.cpp
    src/federation/grid_identity.cpp
    src/federation/grid_identity_store.cpp
    src/federation/travel_token.cpp
    src/federation/replay_cache.cpp
    src/federation/trust_store.cpp
    src/federation/session_store.cpp
    src/federation/runtime.cpp
    src/compat/hypergrid/xmlrpc.cpp
    src/compat/hypergrid/service.cpp
    src/compat/hypergrid/session_store.cpp
    src/compat/hypergrid/agent_circuit.cpp
    src/compat/hypergrid/home_verifier.cpp
    src/compat/hypergrid/friends_adapter.cpp
    src/compat/hypergrid/asset_adapter.cpp
    src/compat/hypergrid/server.cpp
    src/scripting/script_runtime.cpp
    src/storage/schema_version.cpp
    src/voice/voice_provider.cpp
    src/physics/physics_world.cpp
    src/world/region_runtime.cpp
    src/world/region_persistence.cpp
    src/world/terrain.cpp
    src/world/scene_server.cpp
)
add_library(OpenGenesis::Foundation ALIAS opengenesis-foundation)

target_include_directories(opengenesis-foundation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(opengenesis-foundation PUBLIC Threads::Threads OpenSSL::Crypto)
if(WIN32)
    target_link_libraries(opengenesis-foundation PUBLIC ws2_32)
endif()
target_compile_definitions(opengenesis-foundation PUBLIC OGL_VERSION="${PROJECT_VERSION}")
if(WIN32)
    target_compile_definitions(opengenesis-foundation PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(MSVC)
    target_compile_options(opengenesis-foundation PRIVATE /W4 /permissive- /Zc:__cplusplus)
    if(OGL_ENABLE_WERROR)
        target_compile_options(opengenesis-foundation PRIVATE /WX)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
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

    add_executable(ogl-federation-runtime-tests tests/federation_runtime_tests.cpp)
    target_link_libraries(ogl-federation-runtime-tests PRIVATE OpenGenesis::Foundation)
    add_test(NAME federation-runtime COMMAND ogl-federation-runtime-tests)

    add_executable(ogl-federation-hypergrid-tests tests/federation_hypergrid_tests.cpp)
    target_link_libraries(ogl-federation-hypergrid-tests PRIVATE OpenGenesis::Foundation)
    add_test(NAME federation-hypergrid COMMAND ogl-federation-hypergrid-tests)

    add_executable(ogl-hypergrid-session-tests tests/hypergrid_session_tests.cpp)
    target_link_libraries(ogl-hypergrid-session-tests PRIVATE OpenGenesis::Foundation)
    add_test(NAME hypergrid-session COMMAND ogl-hypergrid-session-tests)

    add_executable(ogl-avatar-hg-social-tests tests/avatar_hg_social_tests.cpp)
    target_link_libraries(ogl-avatar-hg-social-tests PRIVATE OpenGenesis::Foundation)
    add_test(NAME avatar-hg-social COMMAND ogl-avatar-hg-social-tests)
endif()
