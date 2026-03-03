cmake_minimum_required(VERSION 3.15)
project(WebAgent VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Указываем компилятору искать заголовочные файлы в вашей папке include
include_directories(include)

# 2. Оставляем поиск OpenSSL (он нужен для HTTPS работы httplib)
find_package(OpenSSL REQUIRED)

add_executable(web_agent
    src/main.cpp
    src/agent.cpp
    src/utils.cpp
)

# 3. Линкуем только OpenSSL
target_link_libraries(web_agent PRIVATE OpenSSL::SSL OpenSSL::Crypto)
