cmake_minimum_required(VERSION 3.15)

project(HopfieldANN
        DESCRIPTION "A Hopfield Artificial Neural Network implementation in C"
        LANGUAGES C
        )

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Library (static) for testing
set(LIB_SOURCE_FILES
    src/HopfieldContext.c
    src/HopfieldCalc.c
    src/HopfieldAnalysis.c
    src/HopfieldNoise.c
    src/HopfieldIO.c
    src/HopfieldUtil.c
)

add_library(hopfield_lib STATIC ${LIB_SOURCE_FILES})
target_include_directories(hopfield_lib PUBLIC src)

if(NOT MSVC)
    target_link_libraries(hopfield_lib PRIVATE m)
endif()

if(MSVC)
    target_compile_options(hopfield_lib PRIVATE /W4)
else()
    target_compile_options(hopfield_lib PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -Werror=implicit-function-declaration
    )
endif()

# Main executable
add_executable(hopfieldann src/main.c src/cli.c)
target_link_libraries(hopfieldann PRIVATE hopfield_lib)
target_include_directories(hopfieldann PRIVATE src)

set_target_properties(hopfieldann PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
)

install(TARGETS hopfieldann RUNTIME DESTINATION ".")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/data" DESTINATION ".")

# Tests can be disabled for a C-only application build.
include(CTest)
if(BUILD_TESTING)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(GTest REQUIRED)

    add_executable(hopfield_test test/testHopfieldCalc.cpp)
    # FindGTest before CMake 3.20 exposes the older imported target name.
    if(TARGET GTest::gtest_main)
        target_link_libraries(hopfield_test PRIVATE GTest::gtest_main hopfield_lib)
    else()
        target_link_libraries(hopfield_test PRIVATE GTest::Main hopfield_lib)
    endif()

    add_test(NAME HopfieldTest COMMAND hopfield_test
             WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

    # CLI contract tests require Bash on Unix.
    if(UNIX)
        find_program(BASH_EXECUTABLE NAMES bash)
        if(BASH_EXECUTABLE)
            add_test(NAME CliContract
                     COMMAND "${BASH_EXECUTABLE}"
                             "${PROJECT_SOURCE_DIR}/testScripts/cli_contract_test.sh"
                             $<TARGET_FILE:hopfieldann>
                     WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
            add_test(NAME ExperimentContract
                     COMMAND "${BASH_EXECUTABLE}"
                             "${PROJECT_SOURCE_DIR}/testScripts/experiment_contract_test.sh"
                             $<TARGET_FILE:hopfieldann>
                     WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        else()
            message(WARNING "Bash not found; CliContract test is disabled")
        endif()
    endif()
endif()
