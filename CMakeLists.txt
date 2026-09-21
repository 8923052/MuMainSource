cmake_minimum_required(VERSION 3.25)

project(MuClient LANGUAGES CXX)

#Set global C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory("src")

if(MSVC)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Main)
    set_property(TARGET Main PROPERTY VS_DEBUGGER_WORKING_DIRECTORY
        "$<TARGET_FILE_DIR:Main>")
endif()
