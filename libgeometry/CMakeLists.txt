set(smd_occt_components FoundationClasses ModelingData ModelingAlgorithms)
if(SMD_BUILD_GEO_VISUALISER)
    list(APPEND smd_occt_components Visualization)
endif()
find_package(OpenCASCADE 8.0.1 EXACT CONFIG QUIET COMPONENTS ${smd_occt_components})
if(NOT OpenCASCADE_FOUND)
    if(SMD_BUILD_GEO_VISUALISER)
        message(FATAL_ERROR "GEO-Visualiser requires visualization-enabled OCCT 8.0.1 exactly (FoundationClasses, ModelingData, ModelingAlgorithms, Visualization). Set OpenCASCADE_DIR to that installation; the headless installation remains supported with SMD_BUILD_GEO_VISUALISER=OFF.")
    endif()
    message(FATAL_ERROR
        "GEO-01A requires installed OCCT 8.0.1 exactly. Set OpenCASCADE_DIR to the directory containing OpenCASCADEConfig.cmake, or set CMAKE_PREFIX_PATH to its installation prefix.")
endif()
if(SMD_BUILD_GEO_VISUALISER)
    # Imported package targets otherwise have directory scope. The opt-in
    # desktop sibling uses the same kernel targets and exact version check.
    foreach(toolkit IN LISTS OpenCASCADE_LIBRARIES)
        if(TARGET ${toolkit})
            set_property(TARGET ${toolkit} PROPERTY IMPORTED_GLOBAL TRUE)
        endif()
    endforeach()
endif()
if(NOT "${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}.${OpenCASCADE_MAINTENANCE_VERSION}" STREQUAL "8.0.1"
   OR NOT "${OpenCASCADE_DEVELOPMENT_VERSION}" STREQUAL "")
    message(FATAL_ERROR "GEO-01A requires OCCT 8.0.1 without a development suffix")
endif()
foreach(toolkit TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO)
    if(NOT TARGET ${toolkit})
        message(FATAL_ERROR "OCCT 8.0.1 installation lacks required target ${toolkit}")
    endif()
endforeach()

find_package(nlohmann_json 3.11 CONFIG REQUIRED)

add_library(smd-geometry STATIC src/faces.cpp src/shell.cpp src/solid.cpp src/principal_prism.cpp
    src/boolean_decomposition.cpp src/export.cpp)
target_include_directories(smd-geometry PUBLIC include)
target_include_directories(smd-geometry SYSTEM PUBLIC "${OpenCASCADE_INCLUDE_DIR}")
target_link_libraries(smd-geometry PUBLIC smd TKBO TKPrim TKTopAlgo TKBRep TKMath TKernel)
target_link_libraries(smd-geometry PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(smd-geometry PUBLIC cxx_std_20)
set_target_properties(smd-geometry PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(smd-geometry PRIVATE /W4 /permissive- /utf-8 /fp:strict)
else()
    target_compile_options(smd-geometry PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

add_executable(smd-geo-export tools/smd_geo_export.cpp)
target_link_libraries(smd-geo-export PRIVATE smd-geometry)
set_target_properties(smd-geo-export PROPERTIES CXX_EXTENSIONS OFF)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(smd_geo_export_cli_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/export_cli.py")

    add_executable(smd-export-tests tests/export_tests.cpp)
    target_link_libraries(smd-export-tests PRIVATE smd-geometry nlohmann_json::nlohmann_json)
    add_test(NAME geometry-export COMMAND smd-export-tests)

    add_executable(smd-export-qualification-tests tests/export_qualification_tests.cpp)
    target_link_libraries(smd-export-qualification-tests PRIVATE smd-geometry)
    set_target_properties(smd-export-qualification-tests PROPERTIES CXX_EXTENSIONS OFF)
    function(add_export_fixture_test name filename additions reductions)
        set(fixture "${PROJECT_SOURCE_DIR}/public/samples/Columns/${filename}")
        add_test(NAME geometry-export-qualification-${name} COMMAND smd-export-qualification-tests
            "${fixture}" ${additions} ${reductions})
        add_test(NAME geometry-export-cli-${name} COMMAND "${Python3_EXECUTABLE}" "${smd_geo_export_cli_test}"
            "$<TARGET_FILE:smd-geo-export>" "${fixture}"
            "$<TARGET_FILE:smd-export-qualification-tests>" ${additions} ${reductions})
    endfunction()
    add_export_fixture_test(plain COL_PLAIN.smd 0 0)
    add_export_fixture_test(pocket COL_SINGLE-POCKET.smd 0 1)
    add_export_fixture_test(corbel COL_PLAIN_SINGLE-CORBEL.smd 1 0)
    add_export_fixture_test(half-corbel COL_PLAIN_HALF-CORBEL.smd 1 0)
    add_export_fixture_test(combined COL_SINGLE-POCKET_SINGLE-CORBEL_02.smd 1 1)
    add_test(NAME geometry-export-qualification-synthetic COMMAND smd-export-qualification-tests synthetic)
    add_test(NAME geometry-export-qualification-negative-controls
        COMMAND smd-export-qualification-tests negative-controls)
    add_test(NAME geometry-export-cli-failures COMMAND "${Python3_EXECUTABLE}" "${smd_geo_export_cli_test}"
        "$<TARGET_FILE:smd-geo-export>" "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN.smd")

    add_executable(smd-boolean-decomposition-tests tests/boolean_decomposition_tests.cpp)
    target_link_libraries(smd-boolean-decomposition-tests PRIVATE smd-geometry)
    add_test(NAME geometry-boolean-decomposition COMMAND smd-boolean-decomposition-tests
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_SINGLE-POCKET.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN_SINGLE-CORBEL.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN_HALF-CORBEL.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_SINGLE-POCKET_SINGLE-CORBEL_02.smd")
    add_executable(smd-principal-prism-tests tests/principal_prism_tests.cpp)
    target_link_libraries(smd-principal-prism-tests PRIVATE smd-geometry)
    add_test(NAME geometry-principal-prism COMMAND smd-principal-prism-tests
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN.smd")
    add_executable(smd-solid-tests tests/solid_tests.cpp)
    target_link_libraries(smd-solid-tests PRIVATE smd-geometry)
    add_test(NAME geometry-solid COMMAND smd-solid-tests
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_SINGLE-POCKET.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN_SINGLE-CORBEL.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN_HALF-CORBEL.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_SINGLE-POCKET_SINGLE-CORBEL_02.smd")
    add_executable(smd-shell-tests tests/shell_tests.cpp)
    target_link_libraries(smd-shell-tests PRIVATE smd-geometry)
    add_test(NAME geometry-shell COMMAND smd-shell-tests
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN.smd")
    add_executable(smd-geometry-tests tests/faces_tests.cpp)
    target_link_libraries(smd-geometry-tests PRIVATE smd-geometry)
    add_test(NAME geometry-faces COMMAND smd-geometry-tests
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_SINGLE-POCKET.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN_SINGLE-CORBEL.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_PLAIN_HALF-CORBEL.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/Columns/COL_SINGLE-POCKET_SINGLE-CORBEL_02.smd")
endif()
