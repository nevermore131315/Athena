cmake_minimum_required(VERSION 3.28)
project(Athena LANGUAGES CXX VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a release build; this is a monitor, not a debug target.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(FTXUI 7 REQUIRED)
find_package(spdlog REQUIRED)

add_executable(athena
    src/main.cpp
    src/gpu.cpp
    src/ram.cpp
    src/cpu.cpp
    src/ui.cpp
)

target_link_libraries(athena PRIVATE ftxui::screen ftxui::dom ftxui::component
                                     spdlog::spdlog_header_only)

install(TARGETS athena RUNTIME DESTINATION bin)
