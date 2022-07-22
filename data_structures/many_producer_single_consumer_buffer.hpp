#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>

#include "../concurrency/memory_order.hpp"

namespace utils_tm
{

// This structure is owned by one thread, and is meant to pass inputs
// to that thread
template <class T, class Allocator = std::allocator<T>, T dummy = T()>
class many_producer_single_consumer_buffer
{
  public:
    using this_type  = many_producer_single_consumer_buffer<T, Allocator>;
    using memo       = concurrency_tm::standard_memory_order_policy;
    using value_type = std::atomic<T>;
    using allocator_type =
        typename std::allocator_traits<Allocator>::rebind_alloc<std::atomic<T>>;
    using alloc_traits = std::allocator_traits<allocator_type>;

  private:
    static constexpr T                   _dummy            = dummy;
    static constexpr size_t              _scnd_buffer_flag = 1ull << 63;
    [[no_unique_address]] allocator_type _allocator;
    size_t                               _capacity;
    std::atomic_size_t                   _pos;
    size_t                               _read_pos;
    size_t                               _read_end;
    std::atomic<T>*                      _buffer;

  public:
    explicit many_producer_single_consumer_buffer(size_t         capacity,
                                                  allocator_type alloc = {});
    explicit many_producer_single_consumer_buffer(
        allocator_type alloc = {}) noexcept;

    many_producer_single_consumer_buffer(
        const many_producer_single_consumer_buffer& other,
        allocator_type                              alloc = {});
    many_producer_single_consumer_buffer&
    operator=(const many_producer_single_consumer_buffer&);

    many_producer_single_consumer_buffer(
        many_producer_single_consumer_buffer&& other) noexcept;
    many_producer_single_consumer_buffer(
        many_producer_single_consumer_buffer&& other,
        allocator_type                         alloc) noexcept;
    many_producer_single_consumer_buffer&
    operator=(many_producer_single_consumer_buffer&& rhs) noexcept;

    ~many_producer_single_consumer_buffer();


    // can be called concurrently by all kinds of threads
    // returns false if the buffer is full
    bool push_back(const T& e);

    template <class iterator_type>
    size_t push_back(iterator_type&       start,
                     const iterator_type& end,
                     size_t               number = 0);

    // can be called concurrent to push_backs but only by the owning thread
    // pull_all breaks all previously pulled elements
    std::optional<T> pop();

    allocator_type get_allocator() const { return _allocator; }

  private:
    void fetch_on_empty_read_buffer();
};




template <class T, class A, T d>
many_producer_single_consumer_buffer<T, A, d>::
    many_producer_single_consumer_buffer(size_t capacity, allocator_type alloc)
    : _allocator(alloc), _capacity(capacity), _pos(0), _read_pos(0),
      _read_end(0)
{
    _buffer = alloc_traits::allocate(_allocator, 2 * capacity);

    for (auto ptr = _buffer; ptr < _buffer + (2 * capacity); ++ptr)
    {
        alloc_traits::construct(_allocator, ptr, _dummy);
    }
}

template <class T, class A, T d>
many_producer_single_consumer_buffer<T, A, d>::
    many_producer_single_consumer_buffer(allocator_type alloc) noexcept
    : _allocator(alloc), _pos(0), _read_pos(0), _read_end(0)
{
    constexpr size_t default_capacity = 64;
    _capacity                         = default_capacity;

    _buffer = alloc_traits::allocate(_allocator, 2 * default_capacity);
    for (auto ptr = _buffer; ptr < _buffer + (2 * default_capacity); ++ptr)
    {
        alloc_traits::construct(_allocator, ptr, _dummy);
    }
}


template <class T, class A, T d>
many_producer_single_consumer_buffer<T, A, d>::
    many_producer_single_consumer_buffer(
        const many_producer_single_consumer_buffer& other, allocator_type alloc)
    : _allocator(alloc), _capacity(other._capacity),
      _pos(other._pos.load(memo::relaxed)), _read_pos(other._read_pos),
      _read_end(other._read_end)
{
    _buffer = alloc_traits::allocate(_allocator, 2 * _capacity);

    for (size_t i = 0; i < 2 * _capacity; ++i)
    {
        alloc_traits::construct(_allocator, _buffer + i,
                                other._buffer[i].load(memo::relaxed));
    }
}

template <class T, class A, T d>
many_producer_single_consumer_buffer<T, A, d>&
many_producer_single_consumer_buffer<T, A, d>::operator=(
    const many_producer_single_consumer_buffer& rhs)
{
    if (&rhs == this) return *this;

    this->~this_type();
    new (this) many_producer_single_consumer_buffer(rhs, rhs.get_allocator());
    return *this;
}



template <class T, class A, T d>
many_producer_single_consumer_buffer<T, A, d>::
    many_producer_single_consumer_buffer(
        many_producer_single_consumer_buffer&& other) noexcept
    : _allocator(other._allocator), _capacity(other._capacity),
      _pos(other._pos.load(memo::relaxed)), _read_pos(other._read_pos),
      _read_end(other._read_end)
{
    _buffer       = other._buffer;
    other._buffer = nullptr;
}

template <class T, class A, T d>
many_producer_single_consumer_buffer<T, A, d>::
    many_producer_single_consumer_buffer(
        many_producer_single_consumer_buffer&& other,
        allocator_type                         alloc) noexcept
    : _allocator(alloc), _capacity(other._capacity),
      _pos(other._pos.load(memo::relaxed)), _read_pos(other._read_pos),
      _read_end(other._read_end)
{
    _buffer       = other._buffer;
    other._buffer = nullptr;
}

template <class T, class A, T d>
many_producer_single_consumer_buffer<T, A, d>&
many_producer_single_consumer_buffer<T, A, d>::operator=(
    many_producer_single_consumer_buffer&& rhs) noexcept
{
    if (&rhs == this) return *this;

    this->~this_type();
    new (this) many_producer_single_consumer_buffer(std::move(rhs));
    return *this;
}





template <class T, class A, T d>
many_producer_single_consumer_buffer<T, A, d>::
    ~many_producer_single_consumer_buffer()
{
    // delete[] _buffer;
    if (!_buffer) return;
    for (auto ptr = _buffer; ptr < _buffer + (2 * _capacity); ++ptr)
    {
        alloc_traits::destroy(_allocator, ptr);
    }
    alloc_traits::deallocate(_allocator, _buffer, 2 * _capacity);
}





template <class T, class A, T d>
bool //
many_producer_single_consumer_buffer<T, A, d>::push_back(const T& e)
{
    auto tpos = _pos.fetch_add(1, memo::acq_rel);

    if (tpos & _scnd_buffer_flag)
    {
        tpos ^= _scnd_buffer_flag;
        if (tpos >= _capacity * 2) return false;
    }
    else if (tpos >= _capacity)
        return false;

    _buffer[tpos].store(e, memo::relaxed);
    return true;
}

template <class T, class A, T d>
template <class iterator_type>
size_t //
many_producer_single_consumer_buffer<T, A, d>::push_back(
    iterator_type& start, const iterator_type& end, size_t number)
{
    if (number == 0) number = end - start;

    size_t tpos   = _pos.fetch_add(number, memo::acq_rel);
    size_t endpos = 0;
    if (tpos & _scnd_buffer_flag)
    {
        tpos ^= _scnd_buffer_flag;
        endpos = std::min(tpos + number, _capacity * 2);
    }
    else
        endpos = std::min(tpos + number, _capacity);

    number = endpos - tpos;

    for (; tpos < endpos; ++tpos)
    {
        _buffer[tpos]->store(*start, memo::release);
        start++;
    }
    return number;
}

template <class T, class A, T d>
std::optional<T> //
many_producer_single_consumer_buffer<T, A, d>::pop()
{
    if (_read_pos == _read_end)
    {
        fetch_on_empty_read_buffer();
        if (_read_pos == _read_end) return {};
    }
    auto read = _buffer[_read_pos].load(memo::relaxed);
    while (read == _dummy) read = _buffer[_read_pos].load(memo::relaxed);
    _buffer[_read_pos].store(_dummy, memo::relaxed);
    ++_read_pos;
    return std::make_optional(read);
}


template <class T, class A, T d>
void //
many_producer_single_consumer_buffer<T, A, d>::fetch_on_empty_read_buffer()
{
    bool first_to_second = !(_scnd_buffer_flag & _pos.load(memo::relaxed));
    if (first_to_second)
    {
        _read_end = _pos.exchange(_capacity | _scnd_buffer_flag, memo::acq_rel);
        _read_end = std::min(_read_end, _capacity);
        _read_pos = 0;
    }
    else
    {
        _read_end = _pos.exchange(0, memo::acq_rel);
        _read_end ^= _scnd_buffer_flag;
        _read_end = std::min(_read_end, 2 * _capacity);
        _read_pos = _capacity;
    }
}



} // namespace utils_tm
