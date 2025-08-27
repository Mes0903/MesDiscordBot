cmake_minimum_required(VERSION 3.22)

project(terry_aoe2_dcbot LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable additional compiler warnings for better code quality
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wunreachable-code)
endif()

set(CPM_DOWNLOAD_VERSION 0.39.0)
set(CPM_SOURCE_CACHE "${CMAKE_BINARY_DIR}/cpm_cache")

if(NOT EXISTS "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
	file(DOWNLOAD
		https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
		${CMAKE_BINARY_DIR}/cmake/CPM.cmake
		TLS_VERIFY ON)
endif()

include(${CMAKE_BINARY_DIR}/cmake/CPM.cmake)

# Add nlohmann/json dependency
CPMAddPackage(
	NAME nlohmann_json
	GITHUB_REPOSITORY nlohmann/json
	VERSION 3.11.3
	OPTIONS
		"JSON_BuildTests OFF"
		"JSON_Install OFF"
)

# Add all source files from src/ directory
file(GLOB SRC_FILES
	CONFIGURE_DEPENDS
	src/*.cpp
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

# Find DPP library
find_package(DPP REQUIRED)

# Set up include directories
target_include_directories(${PROJECT_NAME} PRIVATE 
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_SOURCE_DIR}/include
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
	nlohmann_json::nlohmann_json
	${DPP_LIBRARIES}
)

# Add DPP compile flags
target_compile_options(${PROJECT_NAME} PRIVATE ${DPP_CFLAGS_OTHER})

# Set target properties for better debugging and optimization
set_target_properties(${PROJECT_NAME} PROPERTIES
    DEBUG_POSTFIX d
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Create directories for data files
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Copy .bot_token file if it exists
if(EXISTS ${CMAKE_SOURCE_DIR}/.bot_token)
    configure_file(${CMAKE_SOURCE_DIR}/.bot_token ${CMAKE_BINARY_DIR}/bin/.bot_token COPYONLY)
endif()

# Print configuration info
message(STATUS "Building ${PROJECT_NAME} with C++${CMAKE_CXX_STANDARD}")
message(STATUS "DPP found: ${DPP_FOUND}")
message(STATUS "nlohmann/json found: ${nlohmann_json_FOUND}")
