# Minimum required version of CMake
cmake_minimum_required (VERSION 3.14) # bumped up from 3.10 — FetchContent_MakeAvailable needs 3.14

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("Logic-Gates-Simulator")

#  SET GLOBAL C++ STANDARD HERE (Cleaner and universally supported since CMake 3.1)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Forces the compiler to fail if it doesn't support C++20
set(CMAKE_CXX_EXTENSIONS OFF)      # Disables compiler-specific extensions for pure standard C++

if(MSVC)
        add_compile_options(/permissive-)
        string(REGEX REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# ---------------------------------------------------------------------------
# Dependencies: GLFW + GLAD + GLM
# ---------------------------------------------------------------------------
include(FetchContent)

# GLFW - pulled straight from GitHub, built as part of your project
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)

# GLM - header-only mathematics library
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)

FetchContent_MakeAvailable(glm)

# GLAD - generated at https://glad.dav1d.de and dropped into external/glad/
# expects: external/glad/include/glad/glad.h, external/glad/include/KHR/khrplatform.h,
#          external/glad/src/glad.c
add_library(glad STATIC "external/glad/src/glad.c" "include/Engine/Shader.h" "src/Engine/Engine.cpp" )
target_include_directories(glad PUBLIC "external/glad/include")

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable (Logic-Gates-Simulator 
    "src/main.cpp" 
    "include/main.h"
    "src/Gate.cpp"
    "include/Gate.h" 
    "src/Circuit.cpp" 
    "include/Circuit.h"
    "src/Engine/Shader.cpp" 
    "include/Engine/Shader.h" 
    "src/Engine/Engine.cpp" 
    "include/Engine/Engine.h"
    "src/Engine/ShaderManager.cpp"
    "include/Engine/ShaderManager.h"
)

target_link_libraries(Logic-Gates-Simulator PRIVATE glfw glad glm::glm)

if(WIN32)
    target_link_libraries(Logic-Gates-Simulator PRIVATE opengl32)
endif()
