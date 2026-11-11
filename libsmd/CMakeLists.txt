cmake_minimum_required(VERSION 3.20)
project(libsmd VERSION 0.1.0 LANGUAGES CXX)

add_library(smd STATIC
    src/tokenizer.cpp src/parser10125.cpp src/brep_detector.cpp
    src/transform.cpp src/json.cpp)
target_include_directories(smd PUBLIC include)
target_compile_features(smd PUBLIC cxx_std_20)
set_target_properties(smd PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(smd PRIVATE /W4 /permissive- /utf-8 /fp:strict)
else()
    target_compile_options(smd PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

add_executable(smd-extract tools/smd_extract.cpp)
target_link_libraries(smd-extract PRIVATE smd)

include(CTest)
if(BUILD_TESTING)
    add_executable(smd-native-tests tests/native_tests.cpp)
    target_link_libraries(smd-native-tests PRIVATE smd)
    add_test(NAME native COMMAND smd-native-tests)
    add_executable(smd-token-probe tests/token_probe.cpp)
    target_link_libraries(smd-token-probe PRIVATE smd)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME reference-regression
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare.py"
        --extract "$<TARGET_FILE:smd-extract>" --tokens "$<TARGET_FILE:smd-token-probe>")
endif()
