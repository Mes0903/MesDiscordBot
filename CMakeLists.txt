cmake_minimum_required(VERSION 3.22)

project(TerryDiscordBot LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable additional compiler warnings for better code quality
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow -Wconversion -Wpedantic -Wunreachable-code)
endif()

# nlohmann/json via submodule
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(3rdparty/json)

# Add all source files from src/ directory
file(GLOB SRC_FILES
	CONFIGURE_DEPENDS
  src/*/*.cpp
	src/*.cpp
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

# Find DPP library
find_package(DPP REQUIRED)
# Print configuration info
message(STATUS "Building ${PROJECT_NAME} with C++${CMAKE_CXX_STANDARD}")
message(STATUS "DPP found: ${DPP_FOUND}")

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

# --- Data dir settings (files moved under repo_root/data) ---
set(TERRY_DATA_DIR "${CMAKE_SOURCE_DIR}/data")
file(MAKE_DIRECTORY "${TERRY_DATA_DIR}")

# Create empty JSON files if missing (do NOT overwrite existing data)
if(NOT EXISTS "${TERRY_DATA_DIR}/users.json")
  file(WRITE "${TERRY_DATA_DIR}/users.json" "[]")
endif()
if(NOT EXISTS "${TERRY_DATA_DIR}/matches.json")
  file(WRITE "${TERRY_DATA_DIR}/matches.json" "[]")
endif()

if(EXISTS "${TERRY_DATA_DIR}/.bot_token")
  configure_file("${TERRY_DATA_DIR}/.bot_token"
                 "${CMAKE_BINARY_DIR}/bin/.bot_token" COPYONLY)
endif()

# A convenient 'run' target: run the bot with CWD = data/
add_custom_target(run
  COMMAND ${CMAKE_COMMAND} -E env "TZ=Asia/Taipei"
          ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}
  WORKING_DIRECTORY "${TERRY_DATA_DIR}"
  USES_TERMINAL
)
