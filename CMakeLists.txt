cmake_minimum_required(VERSION 3.16)
project(IntegralCluster CXX)

# C++17 стандарт
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Флаги компиляции
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -O2)
elseif(MSVC)
    add_compile_options(/W4 /O2)
endif()

# Кроссплатформенные библиотеки
if(WIN32)
    set(PLATFORM_LIBS ws2_32)
else()
    find_package(Threads REQUIRED)
    set(PLATFORM_LIBS Threads::Threads m)
endif()

# Сервер
add_executable(server server.cpp)
target_link_libraries(server ${PLATFORM_LIBS})

# Клиент
add_executable(client client.cpp)
target_link_libraries(client ${PLATFORM_LIBS})

# Установка
install(TARGETS server client DESTINATION bin)

# Информация о платформе
message(STATUS "OS: ${CMAKE_SYSTEM_NAME}")
message(STATUS "CPU: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
