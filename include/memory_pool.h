// ============================================================================
// File: memory_pool.h
// Description: High-performance multi-level cache memory pool
// Author: C++ Expert
// License: MIT
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include <array>
#include <thread>
#include <cassert>
#include <algorithm>
#include <type_traits>
#include <unordered_map>

namespace mp {

// ============================================================================
// Forward declarations
// ============================================================================
class MemoryPool;
template <typename T>
class PoolAllocator;

// ============================================================================
// Constants
// ============================================================================
constexpr size_t kAlignment = 8;
constexpr size_t kMaxSmallSize = 256;
constexpr size_t kMaxMediumSize = 4096;
constexpr size_t kPageSize = 4096;
constexpr size_t kSmallSizeClasses = 32;      // 8, 16, 24, 32, ..., 256
constexpr size_t kMediumSizeClasses = 16;     // 512, 1024, ..., 4096
constexpr size_t kThreadCacheMaxBlocks = 64;
constexpr size_t kCentralFreeListMaxBlocks = 256;

// ============================================================================
// Utility functions
// ============================================================================
inline size_t align_up(size_t size, size_t alignment = kAlignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

inline size_t size_to_small_class(size_t size)
{
    return (align_up(size) / kAlignment) - 1;
}

inline size_t size_to_medium_class(size_t size)
{
    size_t aligned = align_up(size, 512);
    return (aligned / 512) - 1 + kSmallSizeClasses;
}

inline size_t class_to_size(size_t class_id)
{
    if (class_id < kSmallSizeClasses)
    {
        return (class_id + 1) * kAlignment;
    }
    else
    {
        return (class_id - kSmallSizeClasses + 1) * 512;
    }
}

// ============================================================================
// SpinLock - Lightweight lock for short critical sections
// ============================================================================
class SpinLock
{
public:
    SpinLock() = default;
    
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    
    void lock() noexcept
    {
        while (flag_.test_and_set(std::memory_order_acquire))
        {
#if defined(__x86_64__) || defined(_M_X64)
            _mm_pause();
#endif
        }
    }
    
    void unlock() noexcept
    {
        flag_.clear(std::memory_order_release);
    }
    
    bool try_lock() noexcept
    {
        return !flag_.test_and_set(std::memory_order_acquire);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// ============================================================================
// Guard - RAII lock guard
// ============================================================================
class Guard
{
public:
    explicit Guard(SpinLock& lock) : lock_(lock)
    {
        lock_.lock();
    }
    
    ~Guard()
    {
        lock_.unlock();
    }
    
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

private:
    SpinLock& lock_;
};

// ============================================================================
// FreeList - Lock-free free list using atomic singly linked list
// ============================================================================
class FreeList
{
public:
    FreeList() : head_(nullptr), size_(0) {}
    
    void push(void* block) noexcept
    {
        Node* node = static_cast<Node*>(block);
        Node* old_head;
        
        do
        {
            old_head = head_.load(std::memory_order_relaxed);
            node->next = old_head;
        }
        while (!head_.compare_exchange_weak(old_head, node,
                                            std::memory_order_release,
                                            std::memory_order_relaxed));
        size_.fetch_add(1, std::memory_order_relaxed);
    }
    
    void* pop() noexcept
    {
        Node* old_head;
        Node* new_head;
        
        do
        {
            old_head = head_.load(std::memory_order_acquire);
            if (!old_head)
            {
                return nullptr;
            }
            new_head = old_head->next;
        }
        while (!head_.compare_exchange_weak(old_head, new_head,
                                            std::memory_order_release,
                                            std::memory_order_relaxed));
        
        size_.fetch_sub(1, std::memory_order_relaxed);
        return old_head;
    }
    
    size_t size() const noexcept
    {
        return size_.load(std::memory_order_relaxed);
    }
    
    bool empty() const noexcept
    {
        return head_.load(std::memory_order_acquire) == nullptr;
    }

private:
    struct Node
    {
        Node* next;
    };
    
    std::atomic<Node*> head_;
    std::atomic<size_t> size_;
};

// ============================================================================
// ThreadCache - Per-thread cache for fast allocation
// ============================================================================
class ThreadCache
{
public:
    ThreadCache() = default;
    
    ~ThreadCache()
    {
        // Return all cached blocks to central cache
        for (size_t i = 0; i < kSmallSizeClasses + kMediumSizeClasses; ++i)
        {
            while (!free_lists_[i].empty())
            {
                void* block = free_lists_[i].pop();
                if (block)
                {
                    MemoryPool::instance().return_to_central(i, block);
                }
            }
        }
    }
    
    void* allocate(size_t size)
    {
        size_t class_id = get_class_id(size);
        if (class_id == SIZE_MAX)
        {
            return nullptr; // Too large for pool
        }
        
        void* block = free_lists_[class_id].pop();
        
        if (!block)
        {
            // Refill from central cache
            block = refill(class_id);
        }
        
        return block;
    }
    
    void deallocate(void* block, size_t size)
    {
        size_t class_id = get_class_id(size);
        if (class_id == SIZE_MAX)
        {
            return; // Too large, should not happen
        }
        
        free_lists_[class_id].push(block);
        
        // Return excess blocks to central cache
        if (free_lists_[class_id].size() > kThreadCacheMaxBlocks * 2)
        {
            release_to_central(class_id, kThreadCacheMaxBlocks);
        }
    }

private:
    size_t get_class_id(size_t size) const
    {
        size_t aligned = align_up(size);
        
        if (aligned <= kMaxSmallSize)
        {
            return size_to_small_class(size);
        }
        else if (aligned <= kMaxMediumSize)
        {
            return size_to_medium_class(size);
        }
        
        return SIZE_MAX; // Too large
    }
    
    void* refill(size_t class_id)
    {
        const size_t batch_size = 16;
        const size_t block_size = class_to_size(class_id);
        
        std::vector<void*> blocks = MemoryPool::instance()
            .fetch_from_central(class_id, batch_size);
        
        if (blocks.empty())
        {
            return nullptr;
        }
        
        // Keep all but one in thread cache
        for (size_t i = 1; i < blocks.size(); ++i)
        {
            free_lists_[class_id].push(blocks[i]);
        }
        
        return blocks[0];
    }
    
    void release_to_central(size_t class_id, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            void* block = free_lists_[class_id].pop();
            if (block)
            {
                MemoryPool::instance().return_to_central(class_id, block);
            }
        }
    }
    
    std::array<FreeList, kSmallSizeClasses + kMediumSizeClasses> free_lists_;
};

// ============================================================================
// CentralCache - Central free lists shared among all threads
// ============================================================================
class CentralCache
{
public:
    void* allocate(size_t class_id)
    {
        Guard guard(locks_[class_id]);
        
        void* block = free_lists_[class_id].pop();
        
        if (!block)
        {
            // Fetch from page cache
            block = fetch_from_page_cache(class_id);
        }
        
        return block;
    }
    
    void deallocate(size_t class_id, void* block)
    {
        Guard guard(locks_[class_id]);
        
        free_lists_[class_id].push(block);
    }
    
    std::vector<void*> batch_allocate(size_t class_id, size_t count)
    {
        Guard guard(locks_[class_id]);
        
        std::vector<void*> blocks;
        blocks.reserve(count);
        
        for (size_t i = 0; i < count; ++i)
        {
            void* block = free_lists_[class_id].pop();
            
            if (!block)
            {
                // Try to get more from page cache
                block = fetch_from_page_cache(class_id);
                if (!block)
                {
                    break;
                }
            }
            
            blocks.push_back(block);
        }
        
        return blocks;
    }
    
    void batch_deallocate(size_t class_id, const std::vector<void*>& blocks)
    {
        Guard guard(locks_[class_id]);
        
        for (void* block : blocks)
        {
            free_lists_[class_id].push(block);
        }
    }

private:
    void* fetch_from_page_cache(size_t class_id)
    {
        size_t block_size = class_to_size(class_id);
        size_t blocks_per_page = kPageSize / block_size;
        
        if (blocks_per_page == 0)
        {
            return nullptr;
        }
        
        // Allocate a new page
        void* page = allocate_page();
        if (!page)
        {
            return nullptr;
        }
        
        // Split page into blocks and add to free list
        char* ptr = static_cast<char*>(page);
        for (size_t i = 0; i < blocks_per_page - 1; ++i)
        {
            free_lists_[class_id].push(ptr);
            ptr += block_size;
        }
        
        // Return the last block
        return ptr;
    }
    
    void* allocate_page()
    {
#ifdef _WIN32
        return VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
        return mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    }
    
    std::array<FreeList, kSmallSizeClasses + kMediumSizeClasses> free_lists_;
    std::array<SpinLock, kSmallSizeClasses + kMediumSizeClasses> locks_;
};

// ============================================================================
// PageCache - Manages large memory pages
// ============================================================================
class PageCache
{
public:
    void* allocate_pages(size_t num_pages)
    {
        Guard guard(lock_);
        
        // Look for existing span in cache
        auto it = free_spans_.find(num_pages);
        if (it != free_spans_.end() && !it->second.empty())
        {
            void* span = it->second.back();
            it->second.pop_back();
            return span;
        }
        
        // Allocate new memory
        size_t size = num_pages * kPageSize;
        
#ifdef _WIN32
        void* memory = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
        void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
        
        return memory;
    }
    
    void deallocate_pages(void* span, size_t num_pages)
    {
        Guard guard(lock_);
        
        free_spans_[num_pages].push_back(span);
    }
    
    static PageCache& instance()
    {
        static PageCache cache;
        return cache;
    }

private:
    PageCache() = default;
    
    std::unordered_map<size_t, std::vector<void*>> free_spans_;
    SpinLock lock_;
};

// ============================================================================
// MemoryPool - Main memory pool class (Singleton)
// ============================================================================
class MemoryPool
{
public:
    static MemoryPool& instance()
    {
        static MemoryPool pool;
        return pool;
    }
    
    // Delete copy/move
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;
    
    void* allocate(size_t size)
    {
        if (size == 0)
        {
            size = 1;
        }
        
        // For large allocations, use system allocator
        if (size > kMaxMediumSize)
        {
            return allocate_large(size);
        }
        
        // Use thread cache
        return get_thread_cache()->allocate(size);
    }
    
    void deallocate(void* block, size_t size)
    {
        if (!block)
        {
            return;
        }
        
        // For large allocations
        if (size > kMaxMediumSize)
        {
            deallocate_large(block, size);
            return;
        }
        
        // Use thread cache
        get_thread_cache()->deallocate(block, size);
    }
    
    // For central cache internal use
    std::vector<void*> fetch_from_central(size_t class_id, size_t count)
    {
        return central_cache_.batch_allocate(class_id, count);
    }
    
    void return_to_central(size_t class_id, void* block)
    {
        central_cache_.deallocate(class_id, block);
    }
    
    // Get statistics
    struct Statistics
    {
        size_t total_allocated = 0;
        size_t total_freed = 0;
        size_t current_usage = 0;
        size_t peak_usage = 0;
    };
    
    Statistics statistics() const
    {
        return stats_;
    }

private:
    MemoryPool()
    {
        stats_ = {};
    }
    
    ~MemoryPool()
    {
        // Cleanup is handled by thread cache destructors
    }
    
    static ThreadCache* get_thread_cache()
    {
        static thread_local ThreadCache cache;
        return &cache;
    }
    
    void* allocate_large(size_t size)
    {
        size_t num_pages = (size + kPageSize - 1) / kPageSize;
        void* memory = page_cache_.allocate_pages(num_pages);
        
        if (memory)
        {
            update_stats(size, true);
        }
        
        return memory;
    }
    
    void deallocate_large(void* block, size_t size)
    {
        size_t num_pages = (size + kPageSize - 1) / kPageSize;
        page_cache_.deallocate_pages(block, num_pages);
        
        update_stats(size, false);
    }
    
    void update_stats(size_t size, bool is_allocate)
    {
        if (is_allocate)
        {
            stats_.total_allocated += size;
            stats_.current_usage += size;
            stats_.peak_usage = std::max(stats_.peak_usage, stats_.current_usage);
        }
        else
        {
            stats_.total_freed += size;
            stats_.current_usage -= size;
        }
    }
    
    CentralCache central_cache_;
    PageCache& page_cache_ = PageCache::instance();
    Statistics stats_;
};

// ============================================================================
// PoolAllocator - STL-compatible allocator
// ============================================================================
template <typename T>
class PoolAllocator
{
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    template <typename U>
    struct rebind
    {
        using other = PoolAllocator<U>;
    };
    
    PoolAllocator() noexcept = default;
    
    template <typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}
    
    pointer allocate(size_type n)
    {
        if (n == 0)
        {
            return nullptr;
        }
        
        if (n > std::numeric_limits<size_type>::max() / sizeof(T))
        {
            throw std::bad_array_new_length();
        }
        
        void* ptr = MemoryPool::instance().allocate(n * sizeof(T));
        
        if (!ptr)
        {
            throw std::bad_alloc();
        }
        
        return static_cast<pointer>(ptr);
    }
    
    void deallocate(pointer p, size_type n) noexcept
    {
        MemoryPool::instance().deallocate(p, n * sizeof(T));
    }
    
    template <typename U, typename... Args>
    void construct(U* p, Args&&... args)
    {
        ::new (p) U(std::forward<Args>(args)...);
    }
    
    template <typename U>
    void destroy(U* p)
    {
        p->~U();
    }
    
    bool operator==(const PoolAllocator&) const noexcept
    {
        return true;
    }
    
    bool operator!=(const PoolAllocator&) const noexcept
    {
        return false;
    }
};

// ============================================================================
// PoolPtr - Smart pointer for pool-allocated objects
// ============================================================================
template <typename T>
class PoolPtr
{
public:
    PoolPtr() : ptr_(nullptr) {}
    
    explicit PoolPtr(T* ptr) : ptr_(ptr) {}
    
    PoolPtr(const PoolPtr&) = delete;
    PoolPtr& operator=(const PoolPtr&) = delete;
    
    PoolPtr(PoolPtr&& other) noexcept : ptr_(other.ptr_)
    {
        other.ptr_ = nullptr;
    }
    
    PoolPtr& operator=(PoolPtr&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    ~PoolPtr()
    {
        reset();
    }
    
    T* get() const noexcept
    {
        return ptr_;
    }
    
    T& operator*() const noexcept
    {
        assert(ptr_);
        return *ptr_;
    }
    
    T* operator->() const noexcept
    {
        assert(ptr_);
        return ptr_;
    }
    
    explicit operator bool() const noexcept
    {
        return ptr_ != nullptr;
    }
    
    void reset() noexcept
    {
        if (ptr_)
        {
            ptr_->~T();
            MemoryPool::instance().deallocate(ptr_, sizeof(T));
            ptr_ = nullptr;
        }
    }
    
    T* release() noexcept
    {
        T* old = ptr_;
        ptr_ = nullptr;
        return old;
    }

private:
    T* ptr_;
};

// ============================================================================
// Helper functions
// ============================================================================
template <typename T, typename... Args>
PoolPtr<T> make_pool_ptr(Args&&... args)
{
    void* memory = MemoryPool::instance().allocate(sizeof(T));
    
    if (!memory)
    {
        throw std::bad_alloc();
    }
    
    try
    {
        T* obj = ::new (memory) T(std::forward<Args>(args)...);
        return PoolPtr<T>(obj);
    }
    catch (...)
    {
        MemoryPool::instance().deallocate(memory, sizeof(T));
        throw;
    }
}

// ============================================================================
// ObjectPool - Typed object pool for specific types
// ============================================================================
template <typename T>
class ObjectPool
{
public:
    explicit ObjectPool(size_t initial_size = 0)
    {
        if (initial_size > 0)
        {
            reserve(initial_size);
        }
    }
    
    ~ObjectPool()
    {
        // All objects should be returned before destruction
        assert(free_list_.size() == capacity_);
    }
    
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    
    template <typename... Args>
    T* construct(Args&&... args)
    {
        void* memory = allocate_block();
        
        if (!memory)
        {
            return nullptr;
        }
        
        return ::new (memory) T(std::forward<Args>(args)...);
    }
    
    void destroy(T* obj)
    {
        if (obj)
        {
            obj->~T();
            deallocate_block(obj);
        }
    }
    
    void reserve(size_t count)
    {
        std::lock_guard<SpinLock> guard(lock_);
        
        for (size_t i = 0; i < count; ++i)
        {
            void* block = allocate_block_internal();
            if (block)
            {
                free_list_.push_back(block);
            }
        }
    }
    
    size_t available() const
    {
        std::lock_guard<SpinLock> guard(lock_);
        return free_list_.size();
    }

private:
    void* allocate_block()
    {
        std::lock_guard<SpinLock> guard(lock_);
        
        if (!free_list_.empty())
        {
            void* block = free_list_.back();
            free_list_.pop_back();
            return block;
        }
        
        return allocate_block_internal();
    }
    
    void deallocate_block(void* block)
    {
        std::lock_guard<SpinLock> guard(lock_);
        free_list_.push_back(block);
    }
    
    void* allocate_block_internal()
    {
        ++capacity_;
        return MemoryPool::instance().allocate(sizeof(T));
    }
    
    std::vector<void*> free_list_;
    size_t capacity_ = 0;
    mutable SpinLock lock_;
};

} // namespace mp

// ============================================================================
// Global new/delete overrides (optional)
// ============================================================================
#ifdef MP_OVERRIDE_GLOBAL_NEW_DELETE

void* operator new(std::size_t size)
{
    void* ptr = mp::MemoryPool::instance().allocate(size);
    if (!ptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete(void* ptr) noexcept
{
    // Note: size information is lost, using 0 for size
    // This is a limitation of global delete override
    mp::MemoryPool::instance().deallocate(ptr, 0);
}

void* operator new[](std::size_t size)
{
    void* ptr = mp::MemoryPool::instance().allocate(size);
    if (!ptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete[](void* ptr) noexcept
{
    mp::MemoryPool::instance().deallocate(ptr, 0);
}

#endif // MP_OVERRIDE_GLOBAL_NEW_DELETE
