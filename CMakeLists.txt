# -------------------------------------------------------------------
# Minimum version of CMake required to configure this project.
# Prevents developers from using an older CMake that doesn't support
# the features used in this project.
# -------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)


# -------------------------------------------------------------------
# Defines the project metadata.
#
# project() is a built-in CMake command.
#
# Name        : Protoscope
# Version     : Used for releases (Semantic Versioning)
# Description : Human-readable description
# Languages   : We are building only C++ code.
#
# Later, if C files are added:
# LANGUAGES C CXX
# -------------------------------------------------------------------
project(
    Protoscope
    VERSION 0.1.0
    DESCRIPTION "Real-time telemetry analysis and anomaly detection framework"
    LANGUAGES CXX
)


# -------------------------------------------------------------------
# Specify which C++ standard should be used.
#
# CMAKE_CXX_STANDARD:
#   Compile using C++20.
#
# CMAKE_CXX_STANDARD_REQUIRED:
#   If the compiler doesn't support C++20,
#   stop the build instead of silently using an older version.
#
# CMAKE_CXX_EXTENSIONS:
#   OFF means use standard ISO C++ only.
#   Avoid compiler-specific extensions for better portability.
# -------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# -------------------------------------------------------------------
# Creates an executable named "protoscope".
#
# Syntax:
# add_executable(<Executable_Name> <Source_Files>)
#
# Every .cpp file that should be compiled into the executable
# must eventually appear here.
#
# Example later:
#
# add_executable(
#     protoscope
#     src/main.cpp
#     src/Processing/RuleEngine.cpp
#     src/Adapters/CSVAdapter.cpp
# )
# -------------------------------------------------------------------
add_executable(
    protoscope
    src/main.cpp
)


# -------------------------------------------------------------------
# Adds additional header search directories.
#
# Syntax:
# target_include_directories(
#     <Target>
#     <Visibility>
#     <Directories>
# )
#
# Target:
#   The executable/library that will use these headers.
#
# PRIVATE:
#   Only this executable can access these headers.
#
# PUBLIC:
#   This target and anything linking against it can use them.
#
# INTERFACE:
#   Only consumers of the target receive these include paths.
#
# ${PROJECT_SOURCE_DIR}
#   Built-in CMake variable.
#   Represents the root directory of the project.
#
# Because Observation.hpp lives inside DataModel/,
# writing:
#
#   #include "Observation.hpp"
#
# works from anywhere in the project without specifying the folder.
# -------------------------------------------------------------------
target_include_directories(
    protoscope
    PRIVATE
    ${PROJECT_SOURCE_DIR}/DataModel
)