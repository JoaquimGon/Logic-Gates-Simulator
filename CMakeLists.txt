cmake_minimum_required(VERSION 3.20)

project(LogicGateSimulator
    VERSION 1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    string(REGEX REPLACE "/W3" "/W4" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

include(FetchContent)

# ============================================================
# GLFW
# ============================================================
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

# ============================================================
# GLM
# ============================================================
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# ============================================================
# GLAD
# ============================================================
add_library(glad STATIC
    external/glad/src/glad.c
)
target_include_directories(glad
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include
)

# ============================================================
# OpenGL
# ============================================================
find_package(OpenGL REQUIRED)

# ============================================================
# Project source files
# ============================================================
set(PROJECT_SOURCES
    src/Core/main.cpp
    src/Core/Engine.cpp
    src/Core/Input.cpp
    src/Core/Scene.cpp

    src/Logic/Circuit.cpp
    src/Logic/Gate.cpp
    src/Logic/Wire.cpp

    src/Graphics/Mesh.cpp
    src/Graphics/Renderer.cpp
    src/Graphics/Shader.cpp
    src/Graphics/ShaderManager.cpp
    src/Graphics/VertexLayout.cpp
)

# ============================================================
# Executable
# ============================================================
add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
)

# ============================================================
# Include directories
# ============================================================
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ============================================================
# Libraries
# ============================================================
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        glad
        glfw
        glm::glm
        OpenGL::GL
)

# ============================================================
# Copy shaders after build
# ============================================================
# Glob all shader files so changes to them are tracked as a dependency
file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*"
)

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/shader_stamp.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/shader_stamp.txt"
    DEPENDS ${SHADER_FILES}
    COMMENT "Copying shaders to output directory..."
)

add_custom_target(CopyShaders DEPENDS "${CMAKE_BINARY_DIR}/shader_stamp.txt")
add_dependencies(${PROJECT_NAME} CopyShaders)

# ============================================================
# Compiler options
# ============================================================
if(MSVC)
    target_compile_options(${PROJECT_NAME}
        PRIVATE
            /permissive-
            /wd4100 # Silence unreferenced formal parameter ('outIndex')
    )
else()
    target_compile_options(${PROJECT_NAME}
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter
    )
endif()

# ============================================================
# Output directories
# ============================================================
set_target_properties(${PROJECT_NAME}
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG
            "${CMAKE_BINARY_DIR}/bin/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE
            "${CMAKE_BINARY_DIR}/bin/Release"
)

# ============================================================
# Visual Studio source grouping
# ============================================================
source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PREFIX "Source Files"
    FILES ${PROJECT_SOURCES}
)

# ============================================================
# Information
# ============================================================
message(STATUS "==============================================")
message(STATUS " ${PROJECT_NAME}")
message(STATUS "==============================================")
message(STATUS " C++ Standard : C++${CMAKE_CXX_STANDARD}")
message(STATUS " Compiler     : ${CMAKE_CXX_COMPILER_ID}")
message(STATUS " Generator    : ${CMAKE_GENERATOR}")
message(STATUS " GLAD         : local GLAD 1")
message(STATUS " GLFW         : 3.4")
message(STATUS " GLM          : 1.0.1")
message(STATUS "==============================================")