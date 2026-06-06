cmake_minimum_required(VERSION 3.10)
project(cpp-memory-pool VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
elseif(MSVC)
    add_compile_options(/W4 /O2)
endif()

# Find threads
find_package(Threads REQUIRED)

# Header-only library
add_library(memory_pool INTERFACE)
target_include_directories(memory_pool INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(memory_pool INTERFACE Threads::Threads)

# Examples
add_executable(basic_usage examples/basic_usage.cpp)
target_link_libraries(basic_usage PRIVATE memory_pool)

add_executable(benchmark examples/benchmark.cpp)
target_link_libraries(benchmark PRIVATE memory_pool)

# Install
install(FILES include/memory_pool.h DESTINATION include)
install(TARGETS memory_pool EXPORT memory_pool-targets)
install(EXPORT memory_pool-targets
    FILE memory_pool-config.cmake
    NAMESPACE memory_pool::
    DESTINATION lib/cmake/memory_pool
)
