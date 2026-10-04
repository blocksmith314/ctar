cmake_minimum_required(VERSION 3.14)
project(ctar VERSION 0.1.0 LANGUAGES C CXX)

MESSAGE(STATUS "PROJECT_NAME ${PROJECT_NAME}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_CLI "Build command‑line executable" ON)
option(BUILD_TEST "Build unit tests" ON)
option(BUILD_BENCHMARKS "Enable benchmarks" OFF)
option(BUILD_ASAN "Enable AddressSanitizer + UBSan for tests only" OFF)

find_package(PkgConfig QUIET)
include(FetchContent)

set(LZ4_LOCAL_DIR "${PROJECT_SOURCE_DIR}/third_party/lz4")

if (EXISTS "${LZ4_LOCAL_DIR}/lib/lz4.c")
    message(STATUS "==== Found local LZ4 source at ${LZ4_LOCAL_DIR}, skip fetch ====")
    set(lz4_SOURCE_DIR "${LZ4_LOCAL_DIR}")
else ()
    message(STATUS "==== Fetch LZ4 from GitHub into ${LZ4_LOCAL_DIR} ====")
    FetchContent_Declare(
            lz4
            GIT_REPOSITORY https://github.com/lz4/lz4.git
            GIT_TAG v1.10.0
            SOURCE_DIR "${LZ4_LOCAL_DIR}"
    )
    FetchContent_MakeAvailable(lz4)
    set(lz4_SOURCE_DIR "${LZ4_LOCAL_DIR}")
endif ()

add_library(lz4 STATIC
        ${lz4_SOURCE_DIR}/lib/lz4.c
        ${lz4_SOURCE_DIR}/lib/lz4hc.c
        ${lz4_SOURCE_DIR}/lib/lz4frame.c
        ${lz4_SOURCE_DIR}/lib/xxhash.c
)
target_include_directories(lz4 PUBLIC ${lz4_SOURCE_DIR}/lib)
add_library(lz4::lz4 ALIAS lz4)

# Warning flags as variable, no INTERFACE target
if (MSVC)
    set(PROJECT_WARNING_FLAGS /W4)
else ()
    set(PROJECT_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

add_library(test_sanitizer_flags INTERFACE)
if (BUILD_ASAN AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    target_compile_options(test_sanitizer_flags INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
    )
    target_link_options(test_sanitizer_flags INTERFACE
            -fsanitize=address,undefined
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
target_compile_options(${PROJECT_NAME}_lib PRIVATE ${PROJECT_WARNING_FLAGS})
target_include_directories(${PROJECT_NAME}_lib PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/api>
        $<BUILD_INTERFACE:${lz4_SOURCE_DIR}/lib>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)


target_link_libraries(${PROJECT_NAME}_lib PRIVATE $<BUILD_INTERFACE:lz4::lz4>)
set_target_properties(${PROJECT_NAME}_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ========= CLI =========
if (BUILD_CLI)
    set(CLI_LIB_SOURCES
            src/cli/arg_parser.cpp
    )

    add_library(${PROJECT_NAME}_cli_lib STATIC ${CLI_LIB_SOURCES})
    target_compile_options(${PROJECT_NAME}_cli_lib PRIVATE ${PROJECT_WARNING_FLAGS})
    target_include_directories(${PROJECT_NAME}_cli_lib PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/api>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${PROJECT_NAME}_cli_lib PUBLIC ${PROJECT_NAME}_lib)

    add_executable(${PROJECT_NAME} src/cli/main.cpp)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    target_compile_options(${PROJECT_NAME} PRIVATE ${PROJECT_WARNING_FLAGS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_cli_lib)
endif ()

# ========= Standalone file_generator =========
if (BUILD_TEST)
    add_executable(file_generator test/tools/file_generator.cpp)
    target_compile_options(file_generator PRIVATE ${PROJECT_WARNING_FLAGS})
    target_compile_definitions(file_generator PRIVATE FILE_GENERATOR_STANDALONE)
endif()

if (BUILD_TEST OR BUILD_BENCHMARKS)
    add_library(test_tools STATIC
            test/tools/dir_compare.cpp
            test/tools/file_generator.cpp
    )
    target_compile_options(test_tools PRIVATE ${PROJECT_WARNING_FLAGS})
    target_include_directories(test_tools PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/api>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(test_tools PUBLIC ${PROJECT_NAME}_lib)
endif()

# ========= Unit Tests =========
if (BUILD_TEST)
    add_custom_target(all_tests)
    enable_testing()
    message(STATUS "BUILD_TEST enabled")

    add_library(test_warning_fix INTERFACE)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(test_warning_fix INTERFACE "-Wno-maybe-uninitialized")
    endif ()

    set(GTEST_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/googletest")
    if (EXISTS "${GTEST_SOURCE_DIR}/CMakeLists.txt")
        message(STATUS "[TEST] Use local googletest at third_party/googletest")
        add_subdirectory(${GTEST_SOURCE_DIR} EXCLUDE_FROM_ALL)
    else ()
        message(STATUS "[TEST] Fetch googletest from GitHub")
        FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG v1.15.2
                SOURCE_DIR "${GTEST_SOURCE_DIR}"
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
        target_compile_options(${TEST_NAME} PRIVATE ${PROJECT_WARNING_FLAGS})
        set(BASE_TEST_LIBS
                gmock
                gtest_main
                test_tools
        )
        if (BUILD_CLI AND TEST_NAME MATCHES "test_cli")
            list(APPEND BASE_TEST_LIBS ${PROJECT_NAME}_cli_lib)
        endif ()
        target_link_libraries(${TEST_NAME}
                PRIVATE
                ${BASE_TEST_LIBS}
                test_warning_fix
                test_sanitizer_flags
        )
        target_include_directories(${TEST_NAME} PRIVATE
                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/api>
        )
        gtest_discover_tests(${TEST_NAME})
        add_dependencies(all_tests ${TEST_NAME})
    endforeach ()
endif ()

# ========= Benchmarks =========
if (BUILD_BENCHMARKS)
    include(FetchContent)
    add_library(benchmark_warning_fix INTERFACE)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(benchmark_warning_fix INTERFACE "-Wno-maybe-uninitialized")
    endif ()

    set(BENCHMARK_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/benchmark")
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    if (EXISTS "${BENCHMARK_SOURCE_DIR}/CMakeLists.txt")
        message(STATUS "[BENCHMARK] Use local benchmark at third_party/benchmark")
        add_subdirectory(${BENCHMARK_SOURCE_DIR} EXCLUDE_FROM_ALL)
    else ()
        message(STATUS "[BENCHMARK] Fetch benchmark from GitHub")
        FetchContent_Declare(
                benchmark
                GIT_REPOSITORY https://github.com/google/benchmark.git
                GIT_TAG v1.9.0
                SOURCE_DIR "${BENCHMARK_SOURCE_DIR}"
                BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/benchmark-build"
        )
        FetchContent_MakeAvailable(benchmark)
    endif ()

    set(BENCHMARK_SOURCES
            benchmarks/bench_block_store.cpp
    )
    foreach (BENCHMARK_SOURCE ${BENCHMARK_SOURCES})
        get_filename_component(BENCHMARK_NAME ${BENCHMARK_SOURCE} NAME_WE)
        add_executable(${BENCHMARK_NAME} ${BENCHMARK_SOURCE})
        target_compile_options(${BENCHMARK_NAME} PRIVATE ${PROJECT_WARNING_FLAGS})
        target_link_libraries(${BENCHMARK_NAME}
                PRIVATE
                benchmark::benchmark
                test_tools
                benchmark_warning_fix
        )
        target_include_directories(${BENCHMARK_NAME} PRIVATE
                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/api>
        )
    endforeach ()
endif ()

# ========= Install & CMake package export =========
include(GNUInstallDirs)

# Install library targets
install(TARGETS ${PROJECT_NAME}_lib
        EXPORT ${PROJECT_NAME}Targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if (BUILD_CLI)
    install(TARGETS ${PROJECT_NAME}_cli_lib
            EXPORT ${PROJECT_NAME}Targets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# Install api public headers
install(FILES
        ${PROJECT_SOURCE_DIR}/api/ctar.h
        ${PROJECT_SOURCE_DIR}/api/export.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install dependent src headers, preserve src/core / src/utils folder structure
install(DIRECTORY
        ${PROJECT_SOURCE_DIR}/src/core
        ${PROJECT_SOURCE_DIR}/src/utils
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/src
        FILES_MATCHING
        PATTERN "block_store.h"
        PATTERN "status.h"
)

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
        ${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

# ========= pkg‑config .pc file =========
set(prefix "${CMAKE_INSTALL_PREFIX}")
set(exec_prefix "\${prefix}")
set(libdir "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
set(includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")

configure_file(
        ${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}.pc.in
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.pc
        @ONLY
)

install(FILES
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)