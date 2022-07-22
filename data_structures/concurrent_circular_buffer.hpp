
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>

#include "../concurrency/memory_order.hpp"

namespace utils_tm
{

template <class T, class Allocator = std::allocator<T>, T dummy = T()>
class concurrent_circular_buffer
{
  public:
    using this_type  = concurrent_circular_buffer<T, Allocator, dummy>;
    using memo       = concurrency_tm::standard_memory_order_policy;
    using value_type = std::atomic<T>;
    using allocator_type =
        typename std::allocator_traits<Allocator>::rebind_alloc<std::atomic<T>>;
    using alloc_traits = std::allocator_traits<allocator_type>;

  private:
    static constexpr T                   _dummy = dummy;
    [[no_unique_address]] allocator_type _allocator;
    size_t                               _bitmask;
    typename alloc_traits::pointer       _buffer;
    alignas(64) std::atomic_size_t _push_id;
    alignas(64) std::atomic_size_t _pop_id;

  public:
    explicit concurrent_circular_buffer(size_t         capacity,
                                        allocator_type alloc = {});
    explicit concurrent_circular_buffer(allocator_type alloc = {});
    concurrent_circular_buffer(const concurrent_circular_buffer& other,
                               allocator_type                    alloc = {});
    concurrent_circular_buffer& operator=(const concurrent_circular_buffer&);
    concurrent_circular_buffer(concurrent_circular_buffer&& other) noexcept;
    concurrent_circular_buffer(concurrent_circular_buffer&& other,
                               allocator_type               alloc) noexcept;
    concurrent_circular_buffer&
    operator=(concurrent_circular_buffer&& rhs) noexcept;
    ~concurrent_circular_buffer();

    inline void push(T e);
    inline T    pop();

    inline size_t  capacity() const;
    inline size_t  size() const;
    void           clear();
    allocator_type get_allocator() const { return _allocator; }

  private:
    inline size_t mod(size_t i) const { return i & _bitmask; }
};




// CTORS AND DTOR !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
template <class T, class A, T d>
concurrent_circular_buffer<T, A, d>::concurrent_circular_buffer(
    size_t capacity, allocator_type alloc)
    : _allocator(alloc), _push_id(0), _pop_id(0)
{
    size_t tcap = 1;
    while (tcap < capacity) tcap <<= 1;

    // _buffer  = std::make_unique<std::atomic<T>[]>(tcap);
    _bitmask = tcap - 1;
    _buffer  = alloc_traits::allocate(_allocator, tcap);

    using pointer = typename alloc_traits::pointer;
    for (pointer ptr = _buffer; ptr < _buffer + tcap; ++ptr)
    {
        // _buffer[i].store(_dummy, memo::relaxed);
        alloc_traits::construct(_allocator, ptr, _dummy);
    }
}

template <class T, class A, T d>
concurrent_circular_buffer<T, A, d>::concurrent_circular_buffer(
    allocator_type alloc)
    : _allocator(alloc), _push_id(0), _pop_id(0)
{
    size_t default_size = 64; // must be power of 2
    _bitmask            = default_size - 1;
    _buffer             = alloc_traits::allocate(_allocator, default_size);
    using pointer       = typename alloc_traits::pointer;
    for (pointer ptr = _buffer; ptr < _buffer + default_size; ++ptr)
    {
        alloc_traits::construct(_allocator, ptr, _dummy);
    }
}

template <class T, class A, T d>
concurrent_circular_buffer<T, A, d>::concurrent_circular_buffer(
    const concurrent_circular_buffer& other, allocator_type alloc)
    : _allocator(alloc), _bitmask(other._bitmask),
      _push_id(other._push_id.load(memo::relaxed)),
      _pop_id(other._pop_id.load(memo::relaxed))
{
    _buffer = alloc_traits::allocate(_allocator, _bitmask + 1);
    for (size_t i = 0; i <= _bitmask; ++i)
    {
        alloc_traits::construct(_allocator, _buffer + i,
                                other._buffer[i].load(memo::relaxed));
    }
}

template <class T, class A, T d>
concurrent_circular_buffer<T, A, d>&
concurrent_circular_buffer<T, A, d>::operator=(
    const concurrent_circular_buffer& other)
{
    if (&other == this) return *this;

    this->~this_type();
    new (this) concurrent_circular_buffer(other, other.get_allocator());
    return *this;
}


template <class T, class A, T d>
concurrent_circular_buffer<T, A, d>::concurrent_circular_buffer(
    concurrent_circular_buffer&& other) noexcept
    : _allocator(other._allocator), _bitmask(other._bitmask),
      _buffer(other._buffer), _push_id(other._push_id.load()),
      _pop_id(other._pop_id.load())
{
    other._buffer = nullptr;
}

template <class T, class A, T d>
concurrent_circular_buffer<T, A, d>::concurrent_circular_buffer(
    concurrent_circular_buffer&& other, allocator_type alloc) noexcept
    : _allocator(alloc), _bitmask(other._bitmask),
      _buffer(std::move(other._buffer)), _push_id(other._push_id.load()),
      _pop_id(other._pop_id.load())
{
    other._buffer = nullptr;
}

template <class T, class A, T d>
concurrent_circular_buffer<T, A, d>&
concurrent_circular_buffer<T, A, d>::operator=(
    concurrent_circular_buffer&& other) noexcept
{
    if (&other == this) return *this;

    this->~this_type();
    new (this) concurrent_circular_buffer(std::move(other));
    return *this;
}

template <class T, class A, T d>
concurrent_circular_buffer<T, A, d>::~concurrent_circular_buffer()
{
    if (!_buffer) return;

    using pointer = typename alloc_traits::pointer;
    for (pointer ptr = _buffer; ptr < _buffer + capacity(); ++ptr)
    {
        // _buffer[i].store(_dummy, memo::relaxed);
        alloc_traits::destroy(_allocator, ptr);
    }
    alloc_traits::deallocate(_allocator, _buffer, _bitmask + 1);
}




// MAIN FUNCTIONALITY !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
template <class T, class A, T d>
void concurrent_circular_buffer<T, A, d>::push(T e)
{
    auto id = _push_id.fetch_add(1, memo::acquire);
    id      = mod(id + 1);

    auto temp = _dummy;
    while (!_buffer[id].compare_exchange_weak(temp, e, memo::release))
    {
        temp = _dummy;
    }
}

template <class T, class A, T d>
T concurrent_circular_buffer<T, A, d>::pop()
{
    auto id = _pop_id.fetch_add(1, memo::acquire);
    id      = mod(id + 1);

    auto temp = _buffer[id].exchange(_dummy, memo::acq_rel);
    while (temp == _dummy)
    {
        while (_buffer[id].load(memo::relaxed) == _dummy)
        {
            /* wait until something was inserted */
        }
        temp = _buffer[id].exchange(_dummy, memo::acq_rel);
    }
    return temp;
}


// SIZE AND CAPACITY !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

template <class T, class A, T d>
size_t concurrent_circular_buffer<T, A, d>::capacity() const
{
    return _bitmask + 1;
}

template <class T, class A, T d>
size_t concurrent_circular_buffer<T, A, d>::size() const
{
    return _push_id.load(memo::relaxed) - _pop_id.load(memo::relaxed);
}


// HELPER FUNCTIONS !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

template <class T, class A, T d>
void concurrent_circular_buffer<T, A, d>::clear()
{
    // This should be fine although not standard compatible
    // Also, this works only for POD objects
    // T* non_atomic_view = reinterpret_cast<T*>(_buffer.get());
    // std::fill(non_atomic_view, non_atomic_view + _bitmask + 1, _dummy);
}
} // namespace utils_tm
