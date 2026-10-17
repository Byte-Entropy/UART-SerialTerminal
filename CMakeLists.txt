cmake_minimum_required(VERSION 3.16) # May Need to update in future
project(UART_SerialTerminal CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable ROS/catkin dependencies for serial submodule
set(BUILD_TERMIOS OFF CACHE BOOL "" FORCE)

# Add serial submodule from libs
add_subdirectory(libs/serial)

# Core source files
set(CORE_SOURCES
    core/src/main.cpp
    core/src/serial_terminal.cpp
)

# Target executable
add_executable(UART_SerialTerminal ${CORE_SOURCES})

# Include core headers and submodules
target_include_directories(UART_SerialTerminal PRIVATE core/inc)

# Link serial static library
target_link_libraries(UART_SerialTerminal PRIVATE serial)