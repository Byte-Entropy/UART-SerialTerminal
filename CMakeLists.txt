cmake_minimum_required(VERSION 3.16)
project(UART_SerialTerminal CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add serial submodule
add_subdirectory(libs/serial)

# Executable setup
add_executable(UART_SerialTerminal main.cpp)
target_link_libraries(UART_SerialTerminal PRIVATE serial)