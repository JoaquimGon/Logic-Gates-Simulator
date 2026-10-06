include(FetchContent)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(JSON_SystemInclude ON CACHE BOOL "" FORCE)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

if(BUILD_SIMULATOR_APP OR BUILD_TESTING)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glfw)
endif()

if(BUILD_SIMULATOR_APP)
    set(GLAD_SOURCE_FILE "${PROJECT_SOURCE_DIR}/external/glad/src/glad.c")
    set(GLAD_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/external/glad/include")
    if(NOT EXISTS "${GLAD_SOURCE_FILE}" OR NOT EXISTS "${GLAD_INCLUDE_DIR}/glad/glad.h")
        message(FATAL_ERROR "GLAD sources are missing from external/glad/.")
    endif()
    add_library(glad STATIC "${GLAD_SOURCE_FILE}")
    target_include_directories(glad SYSTEM PUBLIC "${GLAD_INCLUDE_DIR}")

    find_package(OpenGL REQUIRED)
endif()

if(BUILD_SIMULATOR_APP OR BUILD_TESTING)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/external/stb_truetype.h")
        message(FATAL_ERROR "Text-rendering dependency is missing from external/stb_truetype.h.")
    endif()
    add_library(stb_truetype INTERFACE)
    target_include_directories(stb_truetype SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/external")
endif()
