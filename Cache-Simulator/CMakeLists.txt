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
)
target_include_directories(cachesim_core PUBLIC include)
cachesim_enable_warnings(cachesim_core)

# Command-line executable.
add_executable(cachesim src/main.cpp)
target_link_libraries(cachesim PRIVATE cachesim_core)
cachesim_enable_warnings(cachesim)

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