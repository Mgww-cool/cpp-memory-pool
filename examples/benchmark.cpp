// ============================================================================
// File: benchmark.cpp
// Description: Performance benchmark comparing memory pool vs standard allocator
// ============================================================================

#include "../include/memory_pool.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>

constexpr int NUM_ALLOCATIONS = 100000;
constexpr int MIN_SIZE = 8;
constexpr int MAX_SIZE = 1024;

// Benchmark standard allocator
void benchmark_standard_allocator()
{
    std::cout << "\nBenchmarking standard allocator..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<void*> ptrs;
    ptrs.reserve(NUM_ALLOCATIONS);
    
    // Allocate
    for (int i = 0; i < NUM_ALLOCATIONS; ++i)
    {
        size_t size = MIN_SIZE + (rand() % (MAX_SIZE - MIN_SIZE));
        void* ptr = malloc(size);
        ptrs.push_back(ptr);
    }
    
    // Deallocate
    for (void* ptr : ptrs)
    {
        free(ptr);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Standard allocator: " << duration.count() << "ms" << std::endl;
}

// Benchmark memory pool
void benchmark_memory_pool()
{
    std::cout << "\nBenchmarking memory pool..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<void*> ptrs;
    ptrs.reserve(NUM_ALLOCATIONS);
    
    // Allocate
    for (int i = 0; i < NUM_ALLOCATIONS; ++i)
    {
        size_t size = MIN_SIZE + (rand() % (MAX_SIZE - MIN_SIZE));
        void* ptr = mp::MemoryPool::instance().allocate(size);
        ptrs.push_back(ptr);
    }
    
    // Deallocate
    for (int i = 0; i < NUM_ALLOCATIONS; ++i)
    {
        size_t size = MIN_SIZE + (rand() % (MAX_SIZE - MIN_SIZE));
        mp::MemoryPool::instance().deallocate(ptrs[i], size);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Memory pool: " << duration.count() << "ms" << std::endl;
}

// Benchmark multithreaded standard allocator
void benchmark_mt_standard(int num_threads)
{
    std::cout << "\nBenchmarking multi-threaded standard allocator (" 
              << num_threads << " threads)..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([]() {
            for (int i = 0; i < NUM_ALLOCATIONS / 4; ++i)
            {
                size_t size = MIN_SIZE + (rand() % (MAX_SIZE - MIN_SIZE));
                void* ptr = malloc(size);
                free(ptr);
            }
        });
    }
    
    for (auto& t : threads)
    {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Standard allocator (MT): " << duration.count() << "ms" << std::endl;
}

// Benchmark multithreaded memory pool
void benchmark_mt_memory_pool(int num_threads)
{
    std::cout << "\nBenchmarking multi-threaded memory pool (" 
              << num_threads << " threads)..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([]() {
            for (int i = 0; i < NUM_ALLOCATIONS / 4; ++i)
            {
                size_t size = MIN_SIZE + (rand() % (MAX_SIZE - MIN_SIZE));
                void* ptr = mp::MemoryPool::instance().allocate(size);
                mp::MemoryPool::instance().deallocate(ptr, size);
            }
        });
    }
    
    for (auto& t : threads)
    {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Memory pool (MT): " << duration.count() << "ms" << std::endl;
}

int main()
{
    std::cout << "Memory Pool Benchmark" << std::endl;
    std::cout << "=====================" << std::endl;
    std::cout << "Allocations: " << NUM_ALLOCATIONS << std::endl;
    std::cout << "Size range: " << MIN_SIZE << " - " << MAX_SIZE << " bytes" << std::endl;
    
    srand(static_cast<unsigned>(time(nullptr)));
    
    // Single-threaded benchmarks
    benchmark_standard_allocator();
    benchmark_memory_pool();
    
    // Multi-threaded benchmarks
    benchmark_mt_standard(4);
    benchmark_mt_memory_pool(4);
    
    return 0;
}
