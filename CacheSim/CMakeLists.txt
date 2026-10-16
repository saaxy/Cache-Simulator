cmake_minimum_required(VERSION 3.14)
project(CacheSim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings: MSVC uses /W4, GCC/Clang/MinGW use -Wall -Wextra -Wpedantic.
function(cachesim_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Core library: all simulation logic lives here so tests and the CLI share it.
add_library(cachesim_core
    src/CacheConfig.cpp
    src/Cache.cpp
    src/Statistics.cpp
    src/TraceParser.cpp
    src/Simulation.cpp
    src/CommandLine.cpp
    src/Report.cpp
    src/TraceGenerator.cpp
    src/Benchmark.cpp
)
target_include_directories(cachesim_core PUBLIC include)
cachesim_enable_warnings(cachesim_core)

# Command-line executable.
add_executable(cachesim src/main.cpp)
target_link_libraries(cachesim PRIVATE cachesim_core)
cachesim_enable_warnings(cachesim)

# Benchmark runner and synthetic-trace generator.
add_executable(cachesim_bench src/bench_main.cpp)
target_link_libraries(cachesim_bench PRIVATE cachesim_core)
cachesim_enable_warnings(cachesim_bench)

add_executable(cachesim_tracegen src/tracegen_main.cpp)
target_link_libraries(cachesim_tracegen PRIVATE cachesim_core)
cachesim_enable_warnings(cachesim_tracegen)

# Tests: plain executables registered with CTest, no external framework.
# cachesim_add_test(foo) builds tests/test_foo.cpp into test_foo and registers "foo".
enable_testing()
function(cachesim_add_test name)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_include_directories(test_${name} PRIVATE tests)
    target_link_libraries(test_${name} PRIVATE cachesim_core)
    cachesim_enable_warnings(test_${name})
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

cachesim_add_test(config)
cachesim_add_test(address)
cachesim_add_test(cache)
cachesim_add_test(organization)
cachesim_add_test(replacement)
cachesim_add_test(write_policy)
cachesim_add_test(trace)
cachesim_add_test(cli)
cachesim_add_test(report)
cachesim_add_test(hierarchy)
cachesim_add_test(generator)
cachesim_add_test(benchmark)
cachesim_add_test(reference)

# End-to-end tests: run the real executables and check their output / exit codes.
set(SAMPLE_TRACE ${CMAKE_SOURCE_DIR}/traces/sample.trace)

add_test(NAME e2e_default_csv
         COMMAND cachesim --trace ${SAMPLE_TRACE} --csv --no-header)
set_tests_properties(e2e_default_csv PROPERTIES PASS_REGULAR_EXPRESSION
    "sample\\.trace,32768,64,4,128,set-associative,LRU,write-back,write-allocate,35,31,4,20,15,0\\.571429,0\\.428571,")

add_test(NAME e2e_help COMMAND cachesim --help)
set_tests_properties(e2e_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: cachesim --trace FILE")

# These must exit with a non-zero status.
add_test(NAME e2e_invalid_geometry COMMAND cachesim --trace ${SAMPLE_TRACE} --size 1000)
add_test(NAME e2e_missing_trace_file COMMAND cachesim --trace ${CMAKE_SOURCE_DIR}/traces/nope.trace)
add_test(NAME e2e_unknown_option COMMAND cachesim --trace ${SAMPLE_TRACE} --bogus)
set_tests_properties(e2e_invalid_geometry e2e_missing_trace_file e2e_unknown_option
                     PROPERTIES WILL_FAIL TRUE)

add_test(NAME e2e_tracegen
         COMMAND cachesim_tracegen --kind sequential --count 3 --write-percent 0)
set_tests_properties(e2e_tracegen PROPERTIES PASS_REGULAR_EXPRESSION
    "R 0x00000000.+R 0x00000004.+R 0x00000008")  # .+ also matches Windows CRLF

add_test(NAME e2e_bench
         COMMAND cachesim_bench --synthetic hotset --synthetic-count 2000)
set_tests_properties(e2e_bench PROPERTIES PASS_REGULAR_EXPRESSION
    "experiment,variant,trace,capacity_bytes.*associativity,full,synthetic:hotset")
