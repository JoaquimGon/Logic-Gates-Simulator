# Override with -DLOGIC_SIMULATOR_FONT=/absolute/path/to/font.ttf.
find_file(LOGIC_SIMULATOR_FONT
    NAMES consola.ttf DejaVuSansMono.ttf LiberationMono-Regular.ttf Arial.ttf
    PATHS
        "${PROJECT_SOURCE_DIR}/assets/fonts"
        "$ENV{WINDIR}/Fonts"
        /usr/share/fonts/truetype/dejavu
        /usr/share/fonts/truetype/liberation2
        /usr/share/fonts/truetype/liberation
        /usr/local/share/fonts
        /System/Library/Fonts/Supplemental
        /Library/Fonts
    DOC "TrueType font used by component labels and the debug overlay"
)
if(NOT LOGIC_SIMULATOR_FONT OR NOT EXISTS "${LOGIC_SIMULATOR_FONT}" OR IS_DIRECTORY "${LOGIC_SIMULATOR_FONT}")
    message(FATAL_ERROR
        "No TrueType font found. Configure with -DLOGIC_SIMULATOR_FONT=/path/to/font.ttf. "
        "For builds without rendering, use -DBUILD_SIMULATOR_APP=OFF."
    )
endif()
file(TO_CMAKE_PATH "${LOGIC_SIMULATOR_FONT}" PROJECT_FONT_PATH)
message(STATUS "Simulator font: ${PROJECT_FONT_PATH}")
