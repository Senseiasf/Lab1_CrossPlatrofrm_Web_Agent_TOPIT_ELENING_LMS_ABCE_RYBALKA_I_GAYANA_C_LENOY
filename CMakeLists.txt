cmake_minimum_required(VERSION 3.15)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.24")
    cmake_policy(SET CMP0135 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(WebAgent)

# --- ГЛОБАЛЬНЫЙ ФИКС: ПОЛНОСТЬЮ ОТКЛЮЧАЕМ ПРАВИЛА УСТАНОВКИ ДЛЯ ВСЕХ БИБЛИОТЕК ---
set(CMAKE_SKIP_INSTALL_RULES TRUE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ==============================================================================
# 1. ЗАГРУЗКА БИБЛИОТЕК
# ==============================================================================

# JSON 
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Базовые настройки CPR 
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(CPR_FORCE_USE_SYSTEM_CURL OFF CACHE BOOL "Force internal CURL" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "Disable ZLIB to reduce dependencies" FORCE)

# Отключаем установку и экспорт, чтобы CMake не падал на генерации файлов
set(CURL_DISABLE_INSTALL ON CACHE BOOL "Disable CURL installation" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "Disable CURL export targets" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "Do not build curl CLI tool" FORCE)
set(CPR_INSTALL OFF CACHE BOOL "Disable CPR installation" FORCE)
set(JSON_Install OFF CACHE BOOL "Disable JSON installation" FORCE)

# ==============================================================================
# 2. КРОССПЛАТФОРМЕННАЯ КРИПТОГРАФИЯ (БЕЗ СИСТЕМНЫХ ЗАВИСИМОСТЕЙ)
# ==============================================================================

if(WIN32)
    # На Windows используем системный Schannel
    set(CPR_FORCE_WINSSL_BACKEND ON CACHE BOOL "" FORCE)
    add_compile_definitions(HAVE_IOCTLSOCKET_FIONBIO=1)
elseif(APPLE)
    # На macOS используем системный Secure Transport
    set(CPR_FORCE_DARWINSSL_BACKEND ON CACHE BOOL "" FORCE)
elseif(UNIX)
    # На Linux КАЧАЕМ исходники MbedTLS, чтобы не просить пользователя ставить пакеты
    set(CPR_FORCE_MBEDTLS_BACKEND ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG v2.28.8
    )
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mbedtls)

    # Жестко прописываем пути, чтобы CURL нашел скачанный MbedTLS, а не искал его в системе
    set(MBEDTLS_FOUND TRUE CACHE BOOL "" FORCE)
    set(MBEDTLS_INCLUDE_DIRS "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
    set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
    set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
endif()

# Загружаем CPR после того, как разобрались с криптографией
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.10.x
)
FetchContent_MakeAvailable(cpr)

# ==============================================================================
# 3. СБОРКА АГЕНТА
# ==============================================================================

add_executable(${PROJECT_NAME}
    main.cpp
    HTTP_client.cpp
    json_functions.cpp
    consoletab.cpp
    logger.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    cpr::cpr
    nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# СТАТИЧЕСКАЯ УПАКОВКА ДЛЯ WINDOWS (.exe без DLL)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(${PROJECT_NAME} PRIVATE "-static" "-static-libgcc" "-static-libstdc++")
    target_link_libraries(${PROJECT_NAME} PRIVATE crypt32 ws2_32 wldap32)
endif()

# СТАТИЧЕСКАЯ УПАКОВКА ДЛЯ LINUX (Автономный .out)
if(UNIX AND NOT APPLE)
    # Вшиваем библиотеки C++, чтобы не было ошибок "libstdc++.so.6 not found"
    target_link_options(${PROJECT_NAME} PRIVATE "-static-libgcc" "-static-libstdc++")
    # Долинковываем скачанный MbedTLS и системные потоки (pthread)
    target_link_libraries(${PROJECT_NAME} PRIVATE mbedtls mbedx509 mbedcrypto pthread dl)
endif()