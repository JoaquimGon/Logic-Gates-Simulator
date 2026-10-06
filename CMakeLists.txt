cmake_minimum_required(VERSION 3.20)
project(LogicGateSimulator VERSION 1.0 LANGUAGES C CXX)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_SIMULATOR_APP "Build the OpenGL simulator application" ON)
include(CTest)
include(cmake/ProjectOptions.cmake)
include(cmake/Dependencies.cmake)

add_library(simulator_components STATIC
    src/Components/Gate.cpp
    src/Components/Definitions/ComponentDefinition.cpp
    src/Components/Definitions/ComponentCatalog.cpp
    src/Components/Definitions/NativeDefinitions.cpp
    src/Components/Definitions/PresentationGeometry.cpp
)
target_link_libraries(simulator_components PUBLIC project_options)

add_library(simulator_simulation STATIC src/Simulation/Circuit.cpp)
target_link_libraries(simulator_simulation PUBLIC simulator_components)

add_library(simulator_geometry STATIC
    src/Geometry/CanvasCamera.cpp
    src/Geometry/Wire.cpp
    src/Geometry/WireNormalization.cpp
    src/Geometry/GeometryQueries.cpp
)
target_link_libraries(simulator_geometry PUBLIC project_options glm::glm)

add_library(simulator_views INTERFACE)
target_link_libraries(simulator_views INTERFACE simulator_simulation glm::glm)

add_library(simulator_component_factory STATIC src/Components/ComponentFactory.cpp)
target_link_libraries(simulator_component_factory PUBLIC simulator_views)

add_library(simulator_scene STATIC
    src/Editor/Scene.cpp
    src/Editor/SceneEditing.cpp
    src/Editor/Connectivity/ConnectivityBuilder.cpp
    src/Editor/Actions/EditorActions.cpp
)
target_link_libraries(simulator_scene PUBLIC
    simulator_simulation simulator_geometry simulator_views simulator_component_factory
)

add_library(simulator_files STATIC src/Persistence/CircuitFile.cpp)
target_link_libraries(simulator_files PUBLIC simulator_scene PRIVATE nlohmann_json::nlohmann_json)

if(BUILD_SIMULATOR_APP OR BUILD_TESTING)
    add_library(simulator_input STATIC
        src/Editor/Input.cpp
        src/Editor/InputRouting.cpp
        src/Editor/CircuitViews.cpp
        src/Editor/ComponentShortcuts.cpp
        src/Editor/Gestures/DragGesture.cpp
        src/Editor/Gestures/WireGesture.cpp
        src/Editor/Gestures/Selection.cpp
    )
    target_link_libraries(simulator_input PUBLIC simulator_scene PRIVATE glfw)
    target_compile_definitions(simulator_input PRIVATE GLFW_INCLUDE_NONE)
    add_library(simulator_ui STATIC src/UI/UI.cpp src/UI/CircuitTabs.cpp)
    target_link_libraries(simulator_ui PUBLIC simulator_input PRIVATE glfw)
    target_compile_definitions(simulator_ui PRIVATE GLFW_INCLUDE_NONE)
endif()

if(BUILD_SIMULATOR_APP OR BUILD_TESTING)
    add_library(simulator_presentation STATIC
        src/Graphics/Presentation/ComponentRenderData.cpp
        src/Graphics/Presentation/ComponentPresentation.cpp
        src/Graphics/Presentation/LabelLayout.cpp
        src/Graphics/Presentation/OverlayPresentation.cpp
        src/Graphics/Text/TextGeometry.cpp
    )
    target_link_libraries(simulator_presentation PUBLIC simulator_views stb_truetype)
endif()

if(TARGET simulator_ui)
    target_link_libraries(simulator_ui PUBLIC simulator_presentation)
endif()

if(BUILD_SIMULATOR_APP)
    include(cmake/Font.cmake)
    add_library(simulator_graphics STATIC
        src/Graphics/Mesh.cpp
        src/Graphics/Renderer.cpp
        src/Graphics/Shader.cpp
        src/Graphics/ShaderManager.cpp
        src/Graphics/VertexLayout.cpp
        src/Graphics/WireGeometry.cpp
        src/Graphics/Text/FontRenderer.cpp
        src/Graphics/Text/TextPainter.cpp
    )
    target_link_libraries(simulator_graphics PUBLIC
        simulator_geometry simulator_presentation glad stb_truetype
        PRIVATE OpenGL::GL
    )
    target_compile_definitions(simulator_graphics PRIVATE
        PROJECT_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets"
        PROJECT_FONT_PATH="${PROJECT_FONT_PATH}"
    )

    target_sources(simulator_ui PRIVATE src/UI/UIDrawing.cpp src/UI/CircuitTabsDrawing.cpp)
    target_link_libraries(simulator_ui PRIVATE simulator_graphics)
    add_executable(${PROJECT_NAME} src/App/main.cpp src/App/Engine.cpp src/App/FileDialog.cpp)
    target_link_libraries(${PROJECT_NAME} PRIVATE simulator_ui simulator_graphics simulator_files glfw)
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE comdlg32)
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE GLFW_INCLUDE_NONE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    )
endif()

if(BUILD_TESTING)
    add_executable(CircuitFileTests tests/CircuitFileTests.cpp)
    target_link_libraries(CircuitFileTests PRIVATE simulator_files nlohmann_json::nlohmann_json)
    foreach(test_name IN ITEMS circuit_json_roundtrip circuit_json_validation circuit_file_io)
        add_test(NAME ${test_name} COMMAND CircuitFileTests ${test_name})
    endforeach()

    add_executable(CameraTests tests/CameraTests.cpp)
    target_link_libraries(CameraTests PRIVATE simulator_geometry)
    foreach(test_name IN ITEMS camera_transforms camera_viewport_layout)
        add_test(NAME ${test_name} COMMAND CameraTests ${test_name})
    endforeach()

    add_executable(LogicTests tests/LogicTests.cpp)
    target_link_libraries(LogicTests PRIVATE simulator_scene)
    foreach(test_name IN ITEMS
        gate_truth_tables component_pin_bounds connection_endpoints
        latch_output_propagation connection_lifecycle scene_pin_layouts
        scene_wire_overlaps scene_feedback source_components
        geometry_services connectivity_builder distinct_route_pins
    )
        add_test(NAME ${test_name} COMMAND LogicTests ${test_name})
    endforeach()

    add_executable(SimulationTests tests/SimulationTests.cpp)
    target_link_libraries(SimulationTests PRIVATE simulator_simulation)
    foreach(test_name IN ITEMS cyclic_propagation clock_edge_timing clock_edge_receivers)
        add_test(NAME ${test_name} COMMAND SimulationTests ${test_name})
    endforeach()

    add_executable(EditorActionsTests tests/EditorActionsTests.cpp)
    target_link_libraries(EditorActionsTests PRIVATE simulator_scene)
    foreach(test_name IN ITEMS
        editor_atomic_batches editor_move_previews editor_configuration
        editor_wire_edits editor_restore_snapshots editor_body_placement editor_component_properties
    )
        add_test(NAME ${test_name} COMMAND EditorActionsTests ${test_name})
    endforeach()

    add_executable(ComponentCatalogTests tests/ComponentCatalogTests.cpp)
    target_link_libraries(ComponentCatalogTests PRIVATE simulator_scene)
    target_compile_definitions(ComponentCatalogTests PRIVATE CATALOG_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
    foreach(test_name IN ITEMS
        component_catalog_defaults component_definition_validation component_catalog_edits component_outputs
    )
        add_test(NAME ${test_name} COMMAND ComponentCatalogTests ${test_name})
    endforeach()

    if(BUILD_SIMULATOR_APP)
        add_executable(RenderTests tests/RenderTests.cpp)
        target_link_libraries(RenderTests PRIVATE simulator_graphics simulator_scene simulator_ui glfw)
        target_compile_definitions(RenderTests PRIVATE GLFW_INCLUDE_NONE
            RENDER_ARTIFACTS_DIR="${CMAKE_BINARY_DIR}/render-artifacts")
        add_test(NAME render_pixels COMMAND RenderTests)
        set_tests_properties(render_pixels PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    add_executable(PresentationTests tests/PresentationTests.cpp)
    target_link_libraries(PresentationTests PRIVATE simulator_scene simulator_presentation)
    foreach(test_name IN ITEMS render_instance_contract component_pin_leads text_presentation)
        add_test(NAME ${test_name} COMMAND PresentationTests ${test_name})
    endforeach()

    add_executable(InputTests tests/InputTests.cpp)
    target_link_libraries(InputTests PRIVATE simulator_ui glfw)
    target_compile_definitions(InputTests PRIVATE GLFW_INCLUDE_NONE)
    foreach(test_name IN ITEMS
        drag_cancellation drag_commit spawn_placement wire_segment_deletion
        interaction_modes mode_cancellation wire_and_pan_gestures
        ui_input_routing ui_gesture_cancellation canvas_input_bounds canvas_camera_interaction
        component_palette component_information component_naming
    )
        add_test(NAME ${test_name} COMMAND InputTests ${test_name})
    endforeach()

    add_executable(CircuitViewsTests tests/CircuitViewsTests.cpp)
    target_link_libraries(CircuitViewsTests PRIVATE simulator_ui simulator_files glfw)
    target_compile_definitions(CircuitViewsTests PRIVATE GLFW_INCLUDE_NONE)
    add_test(NAME circuit_views COMMAND CircuitViewsTests)
endif()
