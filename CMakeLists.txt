cmake_minimum_required(VERSION 3.21)
project(Neu2026 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SRC_FILES "*.cpp")

add_executable(programm
    ${SRC_FILES}
)


# WICHTIG: explizit als normales EXE
set_target_properties(programm PROPERTIES
    WIN32_EXECUTABLE OFF
)