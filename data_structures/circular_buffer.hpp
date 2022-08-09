
#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>

namespace utils_tm
{

// This structure is owned by one thread, and is meant to pass inputs
// to that thread
template <class T, class Allocator = std::allocator<T>>
class circular_buffer
{
  public:
    using this_type  = circular_buffer<T, Allocator>;
    using value_type = T;
    using allocator_type =
        typename std::allocator_traits<Allocator>::rebind_alloc<value_type>;
    using alloc_traits = std::allocator_traits<allocator_type>;
    using pointer      = typename alloc_traits::pointer;


    [[no_unique_address]] allocator_type _allocator;
    size_t                               _start;
    size_t                               _end;
    size_t                               _bitmask;
    T*                                   _buffer;

  public:
    explicit circular_buffer(size_t capacity, allocator_type alloc = {});
    explicit circular_buffer(allocator_type alloc = {});
    circular_buffer(const circular_buffer& other, allocator_type alloc = {});
    circular_buffer& operator=(const circular_buffer&);
    circular_buffer(circular_buffer&& other) noexcept;
    circular_buffer(circular_buffer&& other, allocator_type) noexcept;
    circular_buffer& operator=(circular_buffer&& rhs) noexcept;
    ~circular_buffer();

    inline void push_back(const T& e);
    inline void push_front(const T& e);
    template <class... Args>
    inline void emplace_back(Args&&... args);
    template <class... Args>
    inline void emplace_front(Args&&... args);

    inline std::optional<T> pop_back();
    inline std::optional<T> pop_front();

    size_t         size() const;
    size_t         capacity() const;
    allocator_type get_allocator() const { return _allocator; }

    template <bool is_const>
    class iterator_base
    {
      private:
        using this_type = iterator_base<is_const>;
        friend circular_buffer;

        const circular_buffer* _circular;
        size_t                 _off;

      public:
        using difference_type = std::ptrdiff_t;
        using value_type =
            typename std::conditional<is_const, const T, T>::type;
        using reference         = value_type&;
        using pointer           = value_type*;
        using iterator_category = std::random_access_iterator_tag;

        iterator_base(const circular_buffer* buffer, size_t offset)
            : _circular(buffer), _off(offset)
        {
        }
        iterator_base(const iterator_base& other)            = default;
        iterator_base& operator=(const iterator_base& other) = default;
        ~iterator_base()                                     = default;

        inline reference operator*() const;
        inline pointer   operator->() const;
        inline reference operator[](difference_type d) const;

        inline iterator_base& operator+=(difference_type rhs);
        inline iterator_base& operator-=(difference_type rhs);
        inline iterator_base& operator++();
        inline iterator_base& operator--();
        inline iterator_base  operator+(difference_type rhs) const;
        inline iterator_base  operator-(difference_type rhs) const;

        inline bool operator==(const iterator_base& other) const;
        inline bool operator!=(const iterator_base& other) const;
        inline bool operator<(const iterator_base& other) const;
        inline bool operator>(const iterator_base& other) const;
        inline bool operator<=(const iterator_base& other) const;
        inline bool operator>=(const iterator_base& other) const;
    };

    using iterator       = iterator_base<false>;
    using const_iterator = iterator_base<true>;

    inline iterator       begin() { return iterator(this, _start); }
    inline const_iterator begin() const { return const_iterator(this, _start); }
    inline const_iterator cbegin() const
    {
        return const_iterator(this, _start);
    }
    inline iterator       end() { return iterator(this, _end); }
    inline const_iterator end() const { return const_iterator(this, _end); }
    inline const_iterator cend() const { return const_iterator(this, _end); }

  private:
    inline size_t mod(size_t i) const { return i & _bitmask; }
    inline size_t inc(size_t i, size_t diff = 1) const { return i + diff; }
    inline size_t dec(size_t i, size_t diff = 1) const { return i - diff; }

    inline size_t compare_offsets(size_t lhs, size_t rhs) const;

    void grow();
    void cleanup();
};




// CTORS AND DTOR !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
template <class T, class A>
circular_buffer<T, A>::circular_buffer(size_t capacity, allocator_type alloc)
    : _allocator(alloc), _start(1ull << 31), _end(1ull << 31)
{
    size_t tcap = 32;
    while (tcap < capacity) tcap <<= 1;

    //_buffer  = static_cast<T*>(malloc(sizeof(T) * tcap));
    _buffer  = alloc_traits::allocate(_allocator, tcap);
    _bitmask = tcap - 1;
}

template <class T, class A>
circular_buffer<T, A>::circular_buffer(allocator_type alloc)
    : _allocator(alloc), _start(1ull << 31), _end(1ull << 31)
{
    size_t default_size = 32; // must be power of 2
    _buffer             = alloc_traits::allocate(_allocator, default_size);
    _bitmask            = default_size - 1;
}

template <class T, class A>
circular_buffer<T, A>::circular_buffer(const circular_buffer& other,
                                       allocator_type         alloc)
    : _allocator(alloc), //
      _bitmask(other._bitmask)
{
    _buffer = alloc_traits::allocate(_allocator, _bitmask + 1);
    for (auto curr : other) { push_back(curr); }
}

template <class T, class A>
circular_buffer<T, A>&
circular_buffer<T, A>::operator=(const circular_buffer& other)
{
    if (&other == this) return *this;

    this->~this_type();
    new (this) circular_buffer(
        other, other.get_allocator()); // in theory we would have to check
                                       // propagate_allocator_on_copy here
    return *this;
}

template <class T, class A>
circular_buffer<T, A>::circular_buffer(circular_buffer&& other) noexcept
    : _bitmask(other._bitmask), _buffer(other._buffer)
{
    other._buffer = nullptr;
}

template <class T, class A>
circular_buffer<T, A>::circular_buffer(circular_buffer&& other,
                                       allocator_type    alloc) noexcept
    : allocator_type(std::move(alloc)), //
      _bitmask(other._bitmask), _buffer(other._buffer)
{
    other._buffer = nullptr;
}

template <class T, class A>
circular_buffer<T, A>&
circular_buffer<T, A>::operator=(circular_buffer&& other) noexcept
{
    if (&other == this) return *this;

    this->~this_type();
    new (this) circular_buffer(std::move(other));
    return *this;
}

template <class T, class A>
circular_buffer<T, A>::~circular_buffer()
{
    if (!_buffer) return;
    cleanup();
    // free(_buffer);
    alloc_traits::deallocate(_allocator, _buffer, _bitmask + 1);
}




// MAIN FUNCTIONALITY !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
template <class T, class A>
void circular_buffer<T, A>::push_back(const T& e)
{

    if (_end > _start + _bitmask) grow();

    //_buffer[mod(_end)] = e;
    alloc_traits::construct(_allocator, _buffer + mod(_end), e);
    _end = inc(_end);
}

template <class T, class A>
void circular_buffer<T, A>::push_front(const T& e)
{
    if (_end > _start + _bitmask) grow();

    _start = dec(_start);
    //_buffer[mod(_start)] = e;
    alloc_traits::construct(_allocator, _buffer + mod(_start), e);
}

template <class T, class A>
template <class... Args>
void circular_buffer<T, A>::emplace_back(Args&&... args)
{

    if (_end > _start + _bitmask) grow();

    // new (&_buffer[mod(_end)]) T(std::forward<Args>(args)...);
    alloc_traits::construct(_allocator, _buffer + mod(_end),
                            std::forward<Args>(args)...);
    _end = inc(_end);
}

template <class T, class A>
template <class... Args>
void circular_buffer<T, A>::emplace_front(Args&&... args)
{
    if (_end > _start + _bitmask) grow();

    _start = dec(_start);
    // new (&_buffer[mod(_start)]) T(std::forward<Args>(args)...);
    alloc_traits::construct(_allocator, _buffer + mod(_start),
                            std::forward<Args>(args)...);
}

template <class T, class A>
std::optional<T> circular_buffer<T, A>::pop_back()
{
    if (_start == _end) { return {}; }

    _end        = dec(_end);
    auto result = std::make_optional(std::move(_buffer[mod(_end)]));
    // _buffer[mod(_end)].~T();
    alloc_traits::destroy(_allocator, _buffer + mod(_end));
    return result;
}

template <class T, class A>
std::optional<T> circular_buffer<T, A>::pop_front()
{
    if (_start == _end) { return {}; }

    auto result = std::make_optional(std::move(_buffer[mod(_start)]));
    // _buffer[mod(_start)].~T();
    alloc_traits::destroy(_allocator, _buffer + mod(_start));
    _start = inc(_start);
    return result;
}


// SIZE AND CAPACITY !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
template <class T, class A>
size_t circular_buffer<T, A>::size() const
{
    return _end - _start;
}

template <class T, class A>
size_t circular_buffer<T, A>::capacity() const
{
    return _bitmask + 1;
}


// HELPER FUNCTIONS !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
template <class T, class A>
size_t circular_buffer<T, A>::compare_offsets(size_t lhs, size_t rhs) const
{
    if (lhs == rhs) return 0;
    return (lhs < rhs) ? -1 : 1;
}

template <class T, class A>
void circular_buffer<T, A>::grow()
{
    auto nbitmask = (_bitmask << 1) + 1;
    // auto nbuffer  = static_cast<T*>(malloc(sizeof(T) * (nbitmask + 1)));
    auto nbuffer = alloc_traits::allocate(_allocator, nbitmask + 1);

    size_t i = 0;
    for (iterator it = begin(); it != end(); ++it)
    {
        nbuffer[i++] = std::move(*it);
        it->~T();
    }

    // free(_buffer);
    alloc_traits::deallocate(_allocator, _buffer, _bitmask + 1);
    _start   = 0;
    _end     = i;
    _bitmask = nbitmask;
    _buffer  = nbuffer;
}

template <class T, class A>
void circular_buffer<T, A>::cleanup()
{
    for (pointer it = _buffer; it <= _buffer + _bitmask; ++it)
    {
        // it->~T();
        alloc_traits::destroy(_allocator, it);
    }
    _start = _end = 0;
}


// ITERATOR STUFF !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// template <class T, class A> template <bool b>
// typename circular_buffer<T, A>::template iterator_base<b>&
// circular_buffer<T, A>::iterator_base<b>::operator=(const iterator_base& rhs)
// {
//     if (this == &rhs) return *this;

//     this->~iterator_base();
//     new (this) iterator_base(rhs);
//     return *this;
// }

template <class T, class A>
template <bool b>
typename circular_buffer<T, A>::template iterator_base<b>::reference
circular_buffer<T, A>::iterator_base<b>::operator*() const
{
    return _circular->_buffer[_circular->mod(_off)];
}

template <class T, class A>
template <bool b>
typename circular_buffer<T, A>::template iterator_base<b>::pointer
circular_buffer<T, A>::iterator_base<b>::operator->() const
{
    return _circular->_buffer + _circular->mod(_off);
}

template <class T, class A>
template <bool b>
typename circular_buffer<T, A>::template iterator_base<b>::reference
circular_buffer<T, A>::iterator_base<b>::operator[](difference_type d) const
{
    return _circular->_buffer[_circular->mod(_circular->inc(_off, d))];
}


template <class T, class A>
template <bool b>
typename circular_buffer<T, A>::template iterator_base<b>&
circular_buffer<T, A>::iterator_base<b>::operator+=(difference_type rhs)
{
    _off = _circular->inc(_off, rhs);
}

template <class T, class A>
template <bool b>
typename circular_buffer<T, A>::template iterator_base<b>&
circular_buffer<T, A>::iterator_base<b>::operator-=(difference_type rhs)
{
    _off = _circular->dec(_off, rhs);
}

template <class T, class A>
template <bool b>
typename circular_buffer<T, A>::template iterator_base<b>&
circular_buffer<T, A>::iterator_base<b>::operator++()
{
    _off = _circular->inc(_off);
    return *this;
}

template <class T, class A>
template <bool b>
typename circular_buffer<T, A>::template iterator_base<b>&
circular_buffer<T, A>::iterator_base<b>::operator--()
{
    _off = _circular->dec(_off);
    return *this;
}

template <class T, class A>
template <bool b>
typename circular_buffer<T, A>::template iterator_base<b>
circular_buffer<T, A>::iterator_base<b>::operator+(difference_type rhs) const
{
    return iterator_base(_circular, _circular->inc(rhs));
}

template <class T, class A>
template <bool b>
typename circular_buffer<T, A>::template iterator_base<b>
circular_buffer<T, A>::iterator_base<b>::operator-(difference_type rhs) const
{
    return iterator_base(_circular, _circular->dec(rhs));
}

template <class T, class A>
template <bool b>
bool circular_buffer<T, A>::iterator_base<b>::operator==(
    const iterator_base& other) const
{
    return (_circular == other._circular) && (_off == other._off);
}

template <class T, class A>
template <bool b>
bool circular_buffer<T, A>::iterator_base<b>::operator!=(
    const iterator_base& other) const
{
    return (_circular != other._circular) || (_off != other._off);
}

template <class T, class A>
template <bool b>
bool circular_buffer<T, A>::iterator_base<b>::operator<(
    const iterator_base& other) const
{
    return _circular->compare_offsets(_off, other._off) < 0;
}

template <class T, class A>
template <bool b>
bool circular_buffer<T, A>::iterator_base<b>::operator>(
    const iterator_base& other) const
{
    return _circular->compare_offsets(_off, other._off) > 0;
}

template <class T, class A>
template <bool b>
bool circular_buffer<T, A>::iterator_base<b>::operator<=(
    const iterator_base& other) const
{
    return _circular->compare_offsets(_off, other._off) <= 0;
}

template <class T, class A>
template <bool b>
bool circular_buffer<T, A>::iterator_base<b>::operator>=(
    const iterator_base& other) const
{
    return _circular->compare_offsets(_off, other._off) >= 0;
}

} // namespace utils_tm
