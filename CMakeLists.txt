cmake_minimum_required(VERSION 3.15)
project(WebAgent)

set(CMAKE_CXX_STANDARD 17)
cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.10.x
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CPR_FORCE_USE_SYSTEM_CURL OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpr)

if(UNIX)
    find_package(CURL REQUIRED)
endif()

add_executable(${PROJECT_NAME}
    main.cpp
    HTTP_client.cpp
    json_functions.cpp
    consoletab.cpp
    logger.cpp
)

if(MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    cpr::cpr
    nlohmann_json::nlohmann_json
)

if(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
endif()

option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(WebAgentTests
        tests/tests.cpp
        json_functions.cpp
        HTTP_client.cpp
        consoletab.cpp
        logger.cpp
    )

    target_include_directories(WebAgentTests PRIVATE ${CMAKE_SOURCE_DIR})

    if(MSVC)
        target_compile_definitions(WebAgentTests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    target_link_libraries(WebAgentTests PRIVATE
        GTest::gtest
        GTest::gtest_main
        nlohmann_json::nlohmann_json
        cpr::cpr
    )

    include(GoogleTest)
    gtest_discover_tests(WebAgentTests)
endif()