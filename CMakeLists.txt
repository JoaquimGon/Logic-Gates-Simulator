# Minimum required version of CMake
cmake_minimum_required (VERSION 3.14) 

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("Logic-Gates-Simulator")

#  SET GLOBAL C++ STANDARD HERE
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON) 
set(CMAKE_CXX_EXTENSIONS OFF)      

if(MSVC)
        add_compile_options(/permissive-)
        string(REGEX REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# ---------------------------------------------------------------------------
# Dependencies: GLFW + GLAD + GLM
# ---------------------------------------------------------------------------
include(FetchContent)

# GLFW
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

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)

FetchContent_MakeAvailable(glm)

# GLAD
add_library(glad STATIC "external/glad/src/glad.c")
target_include_directories(glad PUBLIC "external/glad/include")

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable (Logic-Gates-Simulator 
    # Core
    "src/Core/main.cpp" 
    "src/Core/Engine.cpp" 
    "src/Core/Engine.h"
    "src/Core/Input.cpp" 
    "src/Core/Input.h" 
    "src/Core/Scene.cpp"
    "src/Core/Scene.h"
    
    # Logic
    "src/Logic/Gate.cpp"
    "src/Logic/Gate.h" 
    "src/Logic/Circuit.cpp" 
    "src/Logic/Circuit.h"
    "src/Logic/Wire.cpp"
    "src/Logic/Wire.h"
    "src/Logic/InputPin.h"
    "src/Logic/PinTypes.h"
    "src/Logic/Component.h"
    
    # Graphics
    "src/Graphics/Shader.cpp" 
    "src/Graphics/Shader.h" 
    "src/Graphics/ShaderManager.cpp"
    "src/Graphics/ShaderManager.h"
    "src/Graphics/Mesh.cpp"
    "src/Graphics/Mesh.h" 
    "src/Graphics/VertexLayout.cpp"
    "src/Graphics/VertexLayout.h"
    "src/Graphics/Renderer.cpp"
    "src/Graphics/Renderer.h"
    
    # Views
    "src/Views/GateView.h" 
    "src/Views/GridSystem.h"
    "src/Views/ComponentView.h"
    "src/Views/InputPinView.h"
)

# Tell CMake where to look for your headers now that they are in src/
target_include_directories(Logic-Gates-Simulator PRIVATE src)

target_link_libraries(Logic-Gates-Simulator PRIVATE glfw glad glm::glm)

if(WIN32)
    target_link_libraries(Logic-Gates-Simulator PRIVATE opengl32)
endif()

# ---------------------------------------------------------------------------
# Asset Management
# ---------------------------------------------------------------------------
# 1. Collect all shader files so CMake tracks them as dependencies
file(GLOB_RECURSE SHADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*")

# 2. Create a custom target that removes and re-copies the shader folder
add_custom_target(CopyShaders ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders" "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    SOURCES ${SHADER_FILES}
    COMMENT "Syncing shaders to binary output folder..."
)

# 3. Force your executable to depend on the shader copy target
add_dependencies(Logic-Gates-Simulator CopyShaders)