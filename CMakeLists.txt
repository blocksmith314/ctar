cmake_minimum_required(VERSION 3.14)
project(ctar VERSION 0.1.0 LANGUAGES C CXX)

MESSAGE(STATUS "PROJECT_NAME ${PROJECT_NAME}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_CLI "Build command‑line executable" ON)
option(BUILD_TEST "Build unit‑test" ON)
option(BUILD_BENCHMARKS "Enable benchmarks" ON)
option(BUILD_ASAN "Enable AddressSanitizer + UndefinedBehaviorSanitizer for tests only" OFF)

find_package(PkgConfig QUIET)
include(FetchContent)

set(LZ4_LOCAL_DIR "${PROJECT_SOURCE_DIR}/third_party/lz4")

if (EXISTS "${LZ4_LOCAL_DIR}/lib/lz4.c")
    message(STATUS "==== Found local LZ4 source at ${LZ4_LOCAL_DIR}, skip network download ====")
    set(lz4_SOURCE_DIR "${LZ4_LOCAL_DIR}")
else ()
    message(STATUS "==== Local LZ4 source missing, fetching from GitHub into ${LZ4_LOCAL_DIR} ====")
    FetchContent_Declare(
            lz4
            GIT_REPOSITORY https://github.com/lz4/lz4.git
            GIT_TAG v1.10.0
            GIT_SHALLOW ON
            SOURCE_DIR "${LZ4_LOCAL_DIR}"
    )
    FetchContent_MakeAvailable(lz4)
    set(lz4_SOURCE_DIR "${LZ4_LOCAL_DIR}")
endif ()


message(STATUS "lz4_SOURCE_DIR = ${lz4_SOURCE_DIR}")


add_library(lz4 STATIC
        ${lz4_SOURCE_DIR}/lib/lz4.c
        ${lz4_SOURCE_DIR}/lib/lz4hc.c
        ${lz4_SOURCE_DIR}/lib/lz4frame.c
        ${lz4_SOURCE_DIR}/lib/xxhash.c
)

target_include_directories(lz4 PUBLIC ${lz4_SOURCE_DIR}/lib)
add_library(lz4::lz4 ALIAS lz4)

add_library(${PROJECT_NAME}_warnings INTERFACE)
if (MSVC)
    target_compile_options(${PROJECT_NAME}_warnings INTERFACE /W4)
else ()
    target_compile_options(${PROJECT_NAME}_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif ()

add_library(test_sanitizer_flags INTERFACE)
if (BUILD_ASAN AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    target_compile_options(test_sanitizer_flags INTERFACE
            -fsanitize=address
            -fsanitize=undefined
            -fno-omit-frame-pointer
    )
    target_link_options(test_sanitizer_flags INTERFACE
            -fsanitize=address
            -fsanitize=undefined
    )
endif ()

# ========= Core Library =========
set(CORE_SOURCES
        src/core/block_store.cpp
        src/core/file_format.cpp
        src/core/file_handle.cpp
        src/utils/coding.cpp
        src/thread_pool/thread_pool.cpp
)

add_library(${PROJECT_NAME}_lib STATIC ${CORE_SOURCES})
target_include_directories(${PROJECT_NAME}_lib PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}_lib
        PUBLIC lz4::lz4
        PRIVATE ${PROJECT_NAME}_warnings
)
set_target_properties(${PROJECT_NAME}_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ========= CLI =========
if (BUILD_CLI)
    set(CLI_LIB_SOURCES
            src/cli/arg_parser.cpp
    )

    add_library(${PROJECT_NAME}_cli_lib STATIC ${CLI_LIB_SOURCES})
    target_include_directories(${PROJECT_NAME}_cli_lib PUBLIC ${PROJECT_SOURCE_DIR})
    target_link_libraries(${PROJECT_NAME}_cli_lib
            PUBLIC ${PROJECT_NAME}_lib
            PRIVATE ${PROJECT_NAME}_warnings
    )

    add_executable(${PROJECT_NAME} src/cli/main.cpp)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    target_link_libraries(${PROJECT_NAME}
            PRIVATE ${PROJECT_NAME}_cli_lib
    )
endif ()

# ========= Standalone file_generator =========
if (BUILD_TEST)
    add_executable(file_generator test/tools/file_generator.cpp)
    target_compile_definitions(file_generator PRIVATE FILE_GENERATOR_STANDALONE)
    target_link_libraries(file_generator PRIVATE ${PROJECT_NAME}_warnings)
endif()

if (BUILD_TEST OR BUILD_BENCHMARKS)
    add_library(test_tools STATIC
            test/tools/dir_compare.cpp
            test/tools/file_generator.cpp
    )
    target_include_directories(test_tools PUBLIC ${PROJECT_SOURCE_DIR})
    target_link_libraries(test_tools
            PUBLIC ${PROJECT_NAME}_lib
            PRIVATE ${PROJECT_NAME}_warnings
    )
endif()

# ========= Unit Tests =========
if (BUILD_TEST)
    add_custom_target(all_tests)
    enable_testing()
    message(STATUS "BUILD TEST")
    add_library(test_warning_fix INTERFACE)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(test_warning_fix INTERFACE "-Wno-maybe-uninitialized")
    endif ()

    set(GTEST_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/googletest")
    if (EXISTS "${GTEST_SOURCE_DIR}/CMakeLists.txt")
        message(STATUS "[TEST] Found existing googletest source at third_party/googletest")
        add_subdirectory(${GTEST_SOURCE_DIR} EXCLUDE_FROM_ALL)
    else ()
        message(STATUS "[TEST] googletest not found, auto‑download to third_party/googletest")
        FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG v1.18.0
                GIT_SHALLOW ON
                SOURCE_DIR ${GTEST_SOURCE_DIR}
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif ()

    include(GoogleTest)

    set(TEST_SOURCES
            test/cases/test_block_store.cpp
            test/cases/test_cli.cpp
            test/cases/test_compress.cpp
            test/cases/test_file_format.cpp
            test/cases/test_file_generator.cpp
            test/cases/test_file_handle.cpp
            test/cases/test_serialize_helper.cpp
            test/cases/test_sorted_map.cpp
            test/cases/test_status.cpp
            test/cases/test_thread_pool.cpp
            test/cases/test_xxhash.cpp
    )

    foreach (TEST_SOURCE ${TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SOURCE})
        set(BASE_TEST_LIBS
                gmock
                gtest_main
                test_tools
        )
        if (BUILD_CLI AND TEST_NAME MATCHES "cli")
            list(APPEND BASE_TEST_LIBS ${PROJECT_NAME}_cli_lib)
        endif ()
        target_link_libraries(${TEST_NAME}
                PRIVATE
                ${BASE_TEST_LIBS}
                test_warning_fix
                test_sanitizer_flags
                ${PROJECT_NAME}_warnings
        )
        target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_SOURCE_DIR})
        gtest_discover_tests(${TEST_NAME} TEST_PREFIX "${TEST_NAME}.")
        add_dependencies(all_tests ${TEST_NAME})
    endforeach ()
endif (BUILD_TEST)


# ========== Benchmarks ==========
if (BUILD_BENCHMARKS)
    include(FetchContent)
    add_library(benchmark_warning_fix INTERFACE)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(benchmark_warning_fix INTERFACE "-Wno-maybe-uninitialized")
    endif ()

    set(BENCHMARKS_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/benchmark")
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    if (EXISTS "${BENCHMARKS_SOURCE_DIR}/CMakeLists.txt")
        message(STATUS "[BENCHMARK] Found existing benchmark source at third_party/benchmark")
        add_subdirectory(${BENCHMARKS_SOURCE_DIR} EXCLUDE_FROM_ALL)
    else ()
        message(STATUS "[BENCHMARK] benchmark not found, auto‑download to third_party/benchmark")
        FetchContent_Declare(
                benchmarks
                GIT_REPOSITORY https://github.com/google/benchmark.git
                GIT_TAG v1.9.5
                GIT_SHALLOW ON
                SOURCE_DIR ${BENCHMARKS_SOURCE_DIR}
                BINARY_DIR ${CMAKE_BINARY_DIR}/_deps/benchmark-build
        )
        FetchContent_MakeAvailable(benchmarks)
    endif ()

    set(BENCHMARK_SOURCES
            benchmarks/bench_block_store.cpp
    )
    foreach (BENCHMARK_SOURCE ${BENCHMARK_SOURCES})
        get_filename_component(BENCHMARK_NAME ${BENCHMARK_SOURCE} NAME_WE)
        add_executable(${BENCHMARK_NAME} ${BENCHMARK_SOURCE})

        target_link_libraries(${BENCHMARK_NAME}
                PRIVATE
                benchmark::benchmark
                test_tools
                benchmark_warning_fix
        )
        target_include_directories(${BENCHMARK_NAME} PRIVATE ${PROJECT_SOURCE_DIR})
        target_compile_options(${BENCHMARK_NAME} PRIVATE ${PROJECT_WARNINGS})
    endforeach ()
endif ()
