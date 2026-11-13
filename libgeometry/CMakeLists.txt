find_package(OpenCASCADE 8.0.1 EXACT CONFIG QUIET)
if(NOT OpenCASCADE_FOUND)
    message(FATAL_ERROR
        "GEO-01A requires installed OCCT 8.0.1 exactly. Set OpenCASCADE_DIR to the directory containing OpenCASCADEConfig.cmake, or set CMAKE_PREFIX_PATH to its installation prefix.")
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

add_library(smd-geometry STATIC src/faces.cpp src/shell.cpp src/solid.cpp src/principal_prism.cpp)
target_include_directories(smd-geometry PUBLIC include)
target_include_directories(smd-geometry SYSTEM PUBLIC "${OpenCASCADE_INCLUDE_DIR}")
target_link_libraries(smd-geometry PUBLIC smd TKBO TKPrim TKTopAlgo TKBRep TKMath TKernel)
target_compile_features(smd-geometry PUBLIC cxx_std_20)
set_target_properties(smd-geometry PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(smd-geometry PRIVATE /W4 /permissive- /utf-8 /fp:strict)
else()
    target_compile_options(smd-geometry PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

if(BUILD_TESTING)
    add_executable(smd-principal-prism-tests tests/principal_prism_tests.cpp)
    target_link_libraries(smd-principal-prism-tests PRIVATE smd-geometry)
    add_test(NAME geometry-principal-prism COMMAND smd-principal-prism-tests
        "${PROJECT_SOURCE_DIR}/public/samples/plain_column.smd")
    add_executable(smd-solid-tests tests/solid_tests.cpp)
    target_link_libraries(smd-solid-tests PRIVATE smd-geometry)
    add_test(NAME geometry-solid COMMAND smd-solid-tests
        "${PROJECT_SOURCE_DIR}/public/samples/plain_column.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/pocket_column.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/SingleCorbelColumn.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/SingleHalfCorbelColumn.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/SinglePocketingSingleCorbelColumn.smd")
    add_executable(smd-shell-tests tests/shell_tests.cpp)
    target_link_libraries(smd-shell-tests PRIVATE smd-geometry)
    add_test(NAME geometry-shell COMMAND smd-shell-tests
        "${PROJECT_SOURCE_DIR}/public/samples/plain_column.smd")
    add_executable(smd-geometry-tests tests/faces_tests.cpp)
    target_link_libraries(smd-geometry-tests PRIVATE smd-geometry)
    add_test(NAME geometry-faces COMMAND smd-geometry-tests
        "${PROJECT_SOURCE_DIR}/public/samples/plain_column.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/pocket_column.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/SingleCorbelColumn.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/SingleHalfCorbelColumn.smd"
        "${PROJECT_SOURCE_DIR}/public/samples/SinglePocketingSingleCorbelColumn.smd")
endif()
