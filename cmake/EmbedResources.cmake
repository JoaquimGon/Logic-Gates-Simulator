# Reconfigure when a resource changes, so the executable always contains the current sources and notices.
file(GLOB_RECURSE shader_files CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/shaders/*.vert"
    "${PROJECT_SOURCE_DIR}/assets/shaders/*.frag"
    "${PROJECT_SOURCE_DIR}/assets/shaders/*.glsl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${shader_files} "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/packaging/THIRD-PARTY-NOTICES.txt")

set(embedded_header "#pragma once\n#include <string_view>\n#include <utility>\nnamespace EmbeddedResources {\n")
if(LOGIC_SIMULATOR_PORTABLE)
    string(APPEND embedded_header "inline constexpr std::pair<std::string_view, std::string_view> shaders[] = {\n")
    foreach(shader IN LISTS shader_files)
        file(RELATIVE_PATH name "${PROJECT_SOURCE_DIR}/assets" "${shader}")
        file(READ "${shader}" source)
        if(source MATCHES "\\)lgs_resource\"")
            message(FATAL_ERROR "Embedded resource contains a reserved raw-string delimiter: ${shader}")
        endif()
        string(APPEND embedded_header "{\"${name}\", R\"lgs_resource(${source})lgs_resource\"},\n")
    endforeach()
    string(APPEND embedded_header "};\n")
endif()

file(READ "${PROJECT_SOURCE_DIR}/LICENSE" project_license)
file(READ "${PROJECT_SOURCE_DIR}/packaging/THIRD-PARTY-NOTICES.txt" dependency_notices)
set(notices "Logic Gate Simulator ${PROJECT_VERSION}\nhttps://github.com/JoaquimGon/Logic-Gates-Simulator\n\n${project_license}\n${dependency_notices}")
if(notices MATCHES "\\)lgs_resource\"")
    message(FATAL_ERROR "License notices contain a reserved raw-string delimiter.")
endif()
string(APPEND embedded_header "inline constexpr std::string_view notices = R\"lgs_resource(${notices})lgs_resource\";\n}\n")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedResources.h" CONTENT "${embedded_header}" @ONLY)
