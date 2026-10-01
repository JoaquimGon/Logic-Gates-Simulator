add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_20)
target_include_directories(project_options INTERFACE "${PROJECT_SOURCE_DIR}/src")

if(MSVC)
    target_compile_options(project_options INTERFACE
        /W4 /permissive- /utf-8 /external:W0 /wd4100
    )
else()
    target_compile_options(project_options INTERFACE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )
endif()
