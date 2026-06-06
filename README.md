# C++ Memory Pool

High-performance multi-level cache memory pool implementation for C++11/14/17.

## Features

- **Three-level cache architecture**
  - Thread-local cache (L1) - Lock-free, fastest access
  - Central cache (L2) - Shared among threads with spin locks
  - Page cache (L3) - System memory management

- **Thread-safe**
  - Lock-free free lists using atomic operations
  - Per-thread caches eliminate contention
  - Spin locks for short critical sections

- **Modern C++ design**
  - STL-compatible allocator (`PoolAllocator`)
  - Smart pointer (`PoolPtr`)
  - Type-safe object pool (`ObjectPool`)
  - Move semantics support

- **Performance optimized**
  - Size classes for efficient memory utilization
  - Batch allocation/deallocation
  - Cache-friendly memory layout

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                        │
├─────────────────────────────────────────────────────────────┤
│  PoolAllocator  │  PoolPtr  │  ObjectPool  │  MemoryPool    │
├─────────────────────────────────────────────────────────────┤
│                     Thread Cache (L1)                        │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐          │
│  │ 8 bytes │ │16 bytes │ │24 bytes │ │  ...    │          │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘          │
├─────────────────────────────────────────────────────────────┤
│                     Central Cache (L2)                       │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐          │
│  │ 8 bytes │ │16 bytes │ │24 bytes │ │  ...    │          │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘          │
├─────────────────────────────────────────────────────────────┤
│                     Page Cache (L3)                          │
│  ┌─────────────────────────────────────────────────┐       │
│  │              System Memory (mmap/VirtualAlloc)   │       │
│  └─────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────┘
```

## Size Classes

| Class | Size Range | Alignment |
|-------|------------|----------|
| Small | 8-256 bytes | 8 bytes |
| Medium | 256-4096 bytes | 512 bytes |
| Large | >4096 bytes | Page size |

## Usage

### Basic Allocation

```cpp
#include "memory_pool.h"

// Allocate memory
void* ptr = mp::MemoryPool::instance().allocate(128);

// Use memory
std::memset(ptr, 0, 128);

// Deallocate
mp::MemoryPool::instance().deallocate(ptr, 128);
```

### STL Container Allocator

```cpp
#include <vector>
#include "memory_pool.h"

// Vector with pool allocator
std::vector<int, mp::PoolAllocator<int>> vec;
vec.push_back(42);
vec.push_back(100);
```

### Smart Pointer

```cpp
#include "memory_pool.h"

// Create object with pool allocation
auto ptr = mp::make_pool_ptr<std::string>("Hello");
std::cout << *ptr << std::endl;

// Move semantics
auto ptr2 = std::move(ptr);
```

### Object Pool

```cpp
#include "memory_pool.h"

struct MyObject {
    int x, y;
    MyObject(int x, int y) : x(x), y(y) {}
};

// Create typed object pool
mp::ObjectPool<MyObject> pool(100);

// Allocate object
MyObject* obj = pool.construct(10, 20);

// Deallocate object
pool.destroy(obj);
```

## Build

### Requirements

- C++11 or later
- Linux/macOS/Windows

### Compile

```bash
# Linux/macOS
g++ -std=c++17 -O2 -pthread -I include examples/basic_usage.cpp -o basic_example
g++ -std=c++17 -O2 -pthread -I include examples/benchmark.cpp -o benchmark

# Windows (MSVC)
cl /std:c++17 /O2 /EHsc /I include examples/basic_usage.cpp
```

## Benchmark

Run the benchmark to compare performance:

```bash
./benchmark
```

Example output:

```
Memory Pool Benchmark
=====================
Allocations: 100000
Size range: 8 - 1024 bytes

Benchmarking standard allocator...
Standard allocator: 45ms

Benchmarking memory pool...
Memory pool: 12ms

Benchmarking multi-threaded standard allocator (4 threads)...
Standard allocator (MT): 120ms

Benchmarking multi-threaded memory pool (4 threads)...
Memory pool (MT): 35ms
```

## Design Decisions

1. **Why three levels of caching?**
   - Thread cache eliminates lock contention for frequent allocations
   - Central cache provides shared pool with moderate contention
   - Page cache manages system memory efficiently

2. **Why spin locks instead of mutexes?**
   - Spin locks are faster for short critical sections
   - Avoids context switching overhead
   - Suitable for low-contention scenarios

3. **Why size classes?**
   - Reduces memory fragmentation
   - Enables efficient batch operations
   - Cache-friendly memory layout

## Thread Safety

- Thread-local caches are inherently thread-safe
- Central cache uses atomic free lists with spin locks
- Page cache uses spin lock for span management

## Memory Overhead

- Each block has minimal overhead (hidden in free list pointers)
- Size class alignment may waste up to 7 bytes per allocation
- Page cache maintains spans for reuse

## Limitations

1. Large allocations (>4KB) fall back to page cache directly
2. No memory coalescing for freed blocks
3. Global new/delete override loses size information

## License

MIT License
