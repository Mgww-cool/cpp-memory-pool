// ============================================================================
// File: basic_usage.cpp
// Description: Basic usage examples for memory pool
// ============================================================================

#include "../include/memory_pool.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

// Example 1: Basic allocation/deallocation
void basic_example()
{
    std::cout << "=== Basic Example ===" << std::endl;
    
    // Allocate memory
    void* ptr = mp::MemoryPool::instance().allocate(128);
    
    // Use the memory
    std::memset(ptr, 0, 128);
    
    // Deallocate
    mp::MemoryPool::instance().deallocate(ptr, 128);
    
    std::cout << "Basic allocation/deallocation completed." << std::endl;
}

// Example 2: Using PoolAllocator with STL containers
void stl_example()
{
    std::cout << "\n=== STL Container Example ===" << std::endl;
    
    // Vector with pool allocator
    std::vector<int, mp::PoolAllocator<int>> vec;
    
    for (int i = 0; i < 1000; ++i)
    {
        vec.push_back(i);
    }
    
    std::cout << "Vector size: " << vec.size() << std::endl;
    std::cout << "First element: " << vec[0] << std::endl;
    std::cout << "Last element: " << vec.back() << std::endl;
}

// Example 3: Using PoolPtr smart pointer
void smart_pointer_example()
{
    std::cout << "\n=== Smart Pointer Example ===" << std::endl;
    
    // Create object with pool allocation
    auto ptr = mp::make_pool_ptr<std::string>("Hello, Memory Pool!");
    
    std::cout << "String: " << *ptr << std::endl;
    std::cout << "Length: " << ptr->length() << std::endl;
    
    // Move semantics
    auto ptr2 = std::move(ptr);
    
    if (!ptr)
    {
        std::cout << "Original pointer is now null." << std::endl;
    }
    
    std::cout << "Moved string: " << *ptr2 << std::endl;
}

// Example 4: Using ObjectPool for specific types
struct Particle
{
    float x, y, z;
    float vx, vy, vz;
    int lifetime;
    
    Particle(float x, float y, float z)
        : x(x), y(y), z(z), vx(0), vy(0), vz(0), lifetime(100)
    {
        std::cout << "Particle created at (" << x << ", " << y << ", " << z << ")" << std::endl;
    }
    
    ~Particle()
    {
        std::cout << "Particle destroyed." << std::endl;
    }
};

void object_pool_example()
{
    std::cout << "\n=== Object Pool Example ===" << std::endl;
    
    mp::ObjectPool<Particle> particle_pool(10);
    
    std::cout << "Available particles: " << particle_pool.available() << std::endl;
    
    // Create particles
    Particle* p1 = particle_pool.construct(1.0f, 2.0f, 3.0f);
    Particle* p2 = particle_pool.construct(4.0f, 5.0f, 6.0f);
    
    std::cout << "Available particles after creation: " << particle_pool.available() << std::endl;
    
    // Destroy particles
    particle_pool.destroy(p1);
    particle_pool.destroy(p2);
    
    std::cout << "Available particles after destruction: " << particle_pool.available() << std::endl;
}

// Example 5: Multi-threaded usage
void thread_worker(int thread_id, int iterations)
{
    for (int i = 0; i < iterations; ++i)
    {
        // Allocate
        void* ptr = mp::MemoryPool::instance().allocate(64);
        
        // Simulate work
        std::this_thread::yield();
        
        // Deallocate
        mp::MemoryPool::instance().deallocate(ptr, 64);
    }
}

void multithread_example()
{
    std::cout << "\n=== Multi-threaded Example ===" << std::endl;
    
    const int num_threads = 4;
    const int iterations = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(thread_worker, i, iterations);
    }
    
    for (auto& t : threads)
    {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << num_threads << " threads completed " << iterations 
              << " iterations each in " << duration.count() << "ms" << std::endl;
    
    // Print statistics
    auto stats = mp::MemoryPool::instance().statistics();
    std::cout << "Total allocated: " << stats.total_allocated << " bytes" << std::endl;
    std::cout << "Current usage: " << stats.current_usage << " bytes" << std::endl;
    std::cout << "Peak usage: " << stats.peak_usage << " bytes" << std::endl;
}

int main()
{
    std::cout << "Memory Pool Usage Examples" << std::endl;
    std::cout << "=========================" << std::endl;
    
    basic_example();
    stl_example();
    smart_pointer_example();
    object_pool_example();
    multithread_example();
    
    return 0;
}
