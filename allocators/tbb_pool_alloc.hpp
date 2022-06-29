#pragma once

#include <algorithm>
#include <cstddef>
#include <malloc.h>
#include <type_traits>

#define TBB_PREVIEW_MEMORY_POOL 1
#include "tbb/memory_pool.h"

#include "../concurrency/memory_order.hpp"
#include "../debug.hpp"
namespace dtm = utils_tm::debug_tm;


namespace utils_tm
{
namespace allocators_tm
{
namespace mempool
{
// default pool size is 4 GB;
enum class _pool_state_enum
{
    uninitialized,
    initializing,
    initialized,
    bad_state
};

using _memo                   = concurrency_tm::standard_memory_order_policy;
using _atomic_pool_state_type = std::atomic<_pool_state_enum>;
using _pool_type              = tbb::fixed_pool;

// default 4GB pool size
static std::atomic_size_t _default_pool_size =
    1024ull * 1024ull * 1024ull * 1ull;
static _atomic_pool_state_type _pool_state = _pool_state_enum::uninitialized;
static size_t                  _pool_size  = 1024;
static char*                   _pool_ptr   = new char[_pool_size];
static _pool_type              _pool       = _pool_type(_pool_ptr, _pool_size);


void set_mempool_size(size_t size_in_GB)
{
    dtm::if_debug("Warning: changing mempool size after initialization",
                  _pool_state.load(_memo::acquire) !=
                      _pool_state_enum::uninitialized);
    _default_pool_size.store(size_in_GB * 1024ull * 1024ull * 1024ull,
                             _memo::release);
    dtm::if_debug(
        "Warning: mempool initializaton started while changing mempool size",
        _pool_state.load(_memo::acquire) != _pool_state_enum::uninitialized);
}

// if mempool should be deallocated at some point,
// we should also get a destructor function, i.e., we should get a unique_ptr
bool initialize_mempool(char* ptr, size_t size)
{
    auto temp_state = _pool_state_enum::uninitialized;
    auto temp_bool  = _pool_state.compare_exchange_strong(
         temp_state, _pool_state_enum::initializing, _memo::acq_rel);

    if (!temp_bool)
    {
        dtm::if_debug(
            "Warning: in initialize_mempool -- mempool is already initialized");
        return false;
    }

    _pool.~_pool_type();
    delete[] _pool_ptr;

    _pool_size = size; // should not change concurrently
    _pool_ptr  = ptr;
    new (&_pool) _pool_type(_pool_ptr, _pool_size);
    _pool_state.store(_pool_state_enum::initialized, _memo::release);
    return true;
}

bool _default_mempool_initialization()
{
    auto size = _default_pool_size.load(_memo::acquire);

    _pool.~_pool_type();
    delete[] _pool_ptr;

    auto temp = (char*)malloc(size);
    if (!temp)
    {
        dtm::if_debug("Warning: failure when allocating memory for the "
                      "memory pool");
        _pool_state.store(_pool_state_enum::bad_state);
        return false;
    }
    std::fill(temp, temp + size, 0);

    _pool_size = size;
    _pool_ptr  = temp;
    new (&_pool) _pool_type(_pool_ptr, size);
    _pool_state.store(_pool_state_enum::initialized, _memo::release);

    return true;
}

bool _check_mempool()
{
    auto temp = _pool_state.load(_memo::acquire);
    switch (temp)
    {
    case _pool_state_enum::uninitialized:
        // mempool is uninitialized we might have to do this
        if (_pool_state.compare_exchange_strong(
                temp, _pool_state_enum::initializing, _memo::relaxed))
        {
            // we have to do this (we were the firsts to change the state)
            return _default_mempool_initialization();
        }
        // apparently somebody else is doing this (he came first)
        [[fallthrough]];

    case _pool_state_enum::initializing:
        // somebody else is doing this lets wait for them
        while ((temp = _pool_state.load(_memo::acquire)) ==
               _pool_state_enum::initializing)
        {
            /* waiting for initialization */
        }
        // not initializing anymore, check the current state
        dtm::if_debug("Warning: unexpected memory_pool_state after waiting for "
                      "initialization",
                      temp != _pool_state_enum::initialized);
        [[fallthrough]];

    case _pool_state_enum::initialized:
        // everything is already initialized we're done
        return true;

    case _pool_state_enum::bad_state:
        // whoopsie this is unexpected
        dtm::if_debug("Warning: bad state in the memory_pool");
        return false;
    }
    // this should be unneccessary, but my compiler thinks it is not
    dtm::if_debug("Warning: I THOUGHT THIS WAS UNREACHABLE");
    return false;
}

} // namespace mempool


template <class T>
class tbb_pool_allocator
{
  public:
    using value_type                             = T;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal                        = std::true_type;

    tbb_pool_allocator() noexcept { mempool::_check_mempool(); }
    tbb_pool_allocator(const tbb_pool_allocator&) noexcept = default;
    ~tbb_pool_allocator()                                  = default;

    template <class U>
    tbb_pool_allocator(const tbb_pool_allocator<U>&) noexcept
    {
    }

    T* allocate(std::size_t n)
    {
        return static_cast<T*>(mempool::_pool.malloc(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t) //
    {
        mempool::_pool.free(p);
    }
};

template <class T, class U>
bool operator==(const tbb_pool_allocator<T>&,
                const tbb_pool_allocator<U>&) noexcept
{
    return true;
}

template <class T, class U>
bool operator!=(const tbb_pool_allocator<T>&,
                const tbb_pool_allocator<U>&) noexcept
{
    return false;
}

} // namespace allocators_tm
} // namespace utils_tm
