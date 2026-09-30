cmake_minimum_required(VERSION 3.16)

project(LottoAnalyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

qt_standard_project_setup()

qt_add_executable(LottoAnalyzer
    main.cpp
)

target_link_libraries(LottoAnalyzer
    PRIVATE
        Qt6::Core
)

# Source / execution character set: UTF-8
if(MSVC)
    target_compile_options(LottoAnalyzer PRIVATE /utf-8)
endif()