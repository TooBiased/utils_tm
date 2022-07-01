#pragma once

#include <atomic>
#include <iterator>
#include <memory>

#include "../concurrency/memory_order.hpp"

namespace utils_tm
{

template <class T, class Allocator = std::allocator<T>>
class concurrent_singly_linked_list
{
  private:
    using this_type = concurrent_singly_linked_list<T>;
    using memo      = concurrency_tm::standard_memory_order_policy;

    class queue_item_type
    {
      public:
        queue_item_type(const T& element) : value(element), next(nullptr) {}
        template <class... Args>
        queue_item_type(Args&&... args)
            : value(std::forward<Args>(args)...), next(nullptr)
        {
        }
        T                             value;
        std::atomic<queue_item_type*> next;
    };

    template <bool is_const>
    class iterator_base
    {
      private:
        using this_type = iterator_base<is_const>;
        using item_ptr  = typename std::conditional<is_const,
                                                   const queue_item_type*,
                                                   queue_item_type*>::type;
        item_ptr _ptr;

      public:
        using difference_type = std::ptrdiff_t;
        using value_type =
            typename std::conditional<is_const, const T, T>::type;
        using reference         = value_type&;
        using pointer           = value_type*;
        using iterator_category = std::forward_iterator_tag;

        iterator_base(item_ptr ptr) : _ptr(ptr) {}
        iterator_base(const iterator_base& other)            = default;
        iterator_base& operator=(const iterator_base& other) = default;
        ~iterator_base()                                     = default;

        inline reference operator*() const;
        inline pointer   operator->() const;

        inline iterator_base& operator++();

        inline bool operator==(const iterator_base& other) const;
        inline bool operator!=(const iterator_base& other) const;
    };

  public:
    using value_type          = T;
    using reference_type      = T&;
    using pointer_type        = T*;
    using iterator_type       = iterator_base<false>;
    using const_iterator_type = iterator_base<true>;
    using allocator_type =
        typename std::allocator_traits<Allocator>::rebind_alloc<T>;


  private:
    using internal_allocator_type = typename std::allocator_traits<
        Allocator>::rebind_alloc<queue_item_type>;
    using alloc_traits = std::allocator_traits<allocator_type>;

    using queue_item_ptr = queue_item_type*;

    [[no_unique_address]] internal_allocator_type _allocator;
    std::atomic<queue_item_ptr>                   _head;

  public:
    explicit concurrent_singly_linked_list(allocator_type alloc = {})
        : _allocator(alloc), _head(nullptr)
    {
    }

    concurrent_singly_linked_list(const concurrent_singly_linked_list& other,
                                  allocator_type alloc = {});
    concurrent_singly_linked_list&
    operator=(const concurrent_singly_linked_list&);

    concurrent_singly_linked_list(
        concurrent_singly_linked_list&& source) noexcept;
    concurrent_singly_linked_list(concurrent_singly_linked_list&& source,
                                  allocator_type alloc) noexcept;
    concurrent_singly_linked_list&
    operator=(concurrent_singly_linked_list&& source) noexcept;

    ~concurrent_singly_linked_list();

    template <class... Args>
    inline void emplace(Args&&... args);
    inline void push(const T& element);
    inline void push(queue_item_type* item);

    iterator_type find(const T& element);
    bool          contains(const T& element);

    size_t         size() const;
    allocator_type get_allocator() const { return _allocator; }

    inline iterator_type       begin();
    inline const_iterator_type begin() const;
    inline const_iterator_type cbegin() const;
    inline iterator_type       end();
    inline const_iterator_type end() const;
    inline const_iterator_type cend() const;
};



template <class T, class A>
concurrent_singly_linked_list<T, A>::concurrent_singly_linked_list(
    const concurrent_singly_linked_list& source, allocator_type alloc)
    : _allocator(alloc)
{
    std::atomic<queue_item_ptr>* prev = &_head;
    for (auto curr : source)
    {
        // using push would reorder the queue
        auto temp = alloc_traits::allocate(_allocator, 1);
        alloc_traits::construct(_allocator, temp, curr);
        prev->store(temp, memo::relaxed);
        prev = &(temp->next);
    }
}

template <class T, class A>
concurrent_singly_linked_list<T, A>&
concurrent_singly_linked_list<T, A>::operator=(
    const concurrent_singly_linked_list& source)
{
    if (this == &source) return *this;
    this->~concurrent_singly_linked_list();
    new (this) concurrent_singly_linked_list(source, source.get_allocator());
    return *this;
}



template <class T, class A>
concurrent_singly_linked_list<T, A>::concurrent_singly_linked_list(
    concurrent_singly_linked_list&& source) noexcept
    : _allocator(source._allocator),
      _head(source._head.exchange(nullptr, memo::acq_rel))
{
}

template <class T, class A>
concurrent_singly_linked_list<T, A>::concurrent_singly_linked_list(
    concurrent_singly_linked_list&& source, allocator_type alloc) noexcept
    : _allocator(alloc), //
      _head(source._head.exchange(nullptr, memo::acq_rel))
{
}

template <class T, class A>
concurrent_singly_linked_list<T, A>&
concurrent_singly_linked_list<T, A>::operator=(
    concurrent_singly_linked_list&& source) noexcept
{
    if (this == &source) return *this;
    this->~concurrent_singly_linked_list();
    new (this) concurrent_singly_linked_list(std::move(source));
    return *this;
}


template <class T, class A>
concurrent_singly_linked_list<T, A>::~concurrent_singly_linked_list()
{
    auto temp = _head.exchange(nullptr, memo::relaxed);
    while (temp)
    {
        auto next = temp->next.load(memo::relaxed);
        // delete temp;
        alloc_traits::destroy(_allocator, temp);
        alloc_traits::deallocate(_allocator, temp, 1);
        temp = next;
    }
}



template <class T, class A>
template <class... Args>
void concurrent_singly_linked_list<T, A>::emplace(Args&&... args)
{
    // queue_item_type* item = new queue_item_type(std::forward<Args>(args)...);
    queue_item_type* item = alloc_traits::allocate(_allocator, 1);
    alloc_traits::construct(_allocator, item, std::forward<Args>(args)...);
    push(item);
}

template <class T, class A>
void concurrent_singly_linked_list<T, A>::push(const T& element)
{
    // queue_item_type* item = new queue_item_type{element};
    queue_item_type* item = alloc_traits::allocate(_allocator, 1);
    alloc_traits::construct(_allocator, item, element);
    push(item);
}

template <class T, class A>
void concurrent_singly_linked_list<T, A>::push(queue_item_type* item)
{
    auto temp = _head.load(memo::acquire);
    do {
        item->next.store(temp, memo::relaxed);
    } while (!_head.compare_exchange_weak(temp, item, memo::acq_rel));
}



template <class T, class A>
typename concurrent_singly_linked_list<T, A>::iterator_type
concurrent_singly_linked_list<T, A>::find(const T& element)
{
    return std::find(begin(), end(), element);
}

template <class T, class A>
bool concurrent_singly_linked_list<T, A>::contains(const T& element)
{
    return find(element) != end();
}

template <class T, class A>
size_t concurrent_singly_linked_list<T, A>::size() const
{
    auto result = 0;
    for ([[maybe_unused]] const auto& e : *this) { ++result; }
    return result;
}



// ITERATOR STUFF
template <class T, class A>
typename concurrent_singly_linked_list<T, A>::iterator_type
concurrent_singly_linked_list<T, A>::begin()
{
    return iterator_type(_head.load(memo::acquire));
}

template <class T, class A>
typename concurrent_singly_linked_list<T, A>::const_iterator_type
concurrent_singly_linked_list<T, A>::begin() const
{
    return cbegin();
}

template <class T, class A>
typename concurrent_singly_linked_list<T, A>::const_iterator_type
concurrent_singly_linked_list<T, A>::cbegin() const
{
    return const_iterator_type(_head.load(memo::acquire));
}

template <class T, class A>
typename concurrent_singly_linked_list<T, A>::iterator_type
concurrent_singly_linked_list<T, A>::end()
{
    return iterator_type(nullptr);
}

template <class T, class A>
typename concurrent_singly_linked_list<T, A>::const_iterator_type
concurrent_singly_linked_list<T, A>::end() const
{
    return cend();
}

template <class T, class A>
typename concurrent_singly_linked_list<T, A>::const_iterator_type
concurrent_singly_linked_list<T, A>::cend() const
{
    return const_iterator_type(nullptr);
}



// ITERATOR IMPLEMENTATION
template <class T, class A>
template <bool c>
typename concurrent_singly_linked_list<T,
                                       A>::template iterator_base<c>::reference
concurrent_singly_linked_list<T, A>::iterator_base<c>::operator*() const
{
    return _ptr->value;
}

template <class T, class A>
template <bool c>
typename concurrent_singly_linked_list<T, A>::template iterator_base<c>::pointer
concurrent_singly_linked_list<T, A>::iterator_base<c>::operator->() const
{
    return &(_ptr->value);
}

template <class T, class A>
template <bool c>
typename concurrent_singly_linked_list<T, A>::template iterator_base<
    c>::iterator_base&
concurrent_singly_linked_list<T, A>::iterator_base<c>::operator++()
{
    _ptr = _ptr->next.load(memo::acquire);
    return *this;
}

template <class T, class A>
template <bool c>
bool concurrent_singly_linked_list<T, A>::iterator_base<c>::operator==(
    const iterator_base& other) const
{
    return _ptr == other._ptr;
}

template <class T, class A>
template <bool c>
bool concurrent_singly_linked_list<T, A>::iterator_base<c>::operator!=(
    const iterator_base& other) const
{
    return _ptr != other._ptr;
}
} // namespace utils_tm
