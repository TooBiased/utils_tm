#pragma once

#include <atomic>
#include <memory>
#include <tuple>
#include <vector>

#include "../mark_pointer.hpp"
#include "../output.hpp"

#include "default_destructor.hpp"
#include "reclamation_guard.hpp"

namespace utils_tm
{
namespace reclamation_tm
{

template <class T, class D = default_destructor<T>, class A = std::allocator<T>>
class sequential_manager
{
  public:
    using this_type       = sequential_manager<T, A>;
    using destructor_type = D;
    using allocator_type  = typename std::allocator_traits<A>::rebind_alloc<T>;
    using alloc_traits    = std::allocator_traits<allocator_type>;
    using pointer_type    = T*;
    using atomic_pointer_type = std::atomic<T*>;
    using protected_type      = T;

    template <class lT = T, class lD = default_destructor<T>, class lA = A>
    struct rebind
    {
        using other = sequential_manager<lT, A>;
    };

    sequential_manager(destructor_type&& destructor = {},
                       allocator_type    alloc      = {})
        : _destructor(std::move(destructor)), _allocator(alloc)
    {
    }
    sequential_manager(const sequential_manager&)             = delete;
    sequential_manager& operator=(const sequential_manager&)  = delete;
    sequential_manager(sequential_manager&& other)            = default;
    sequential_manager& operator=(sequential_manager&& other) = default;
    ~sequential_manager()                                     = default;

    class handle_type
    {
      private:
        using parent_type = sequential_manager<T, A>;
        using this_type   = handle_type;

      public:
        using pointer_type        = typename parent_type::pointer_type;
        using atomic_pointer_type = typename parent_type::atomic_pointer_type;
        using guard_type          = reclamation_guard<T, this_type>;

        handle_type(parent_type& parent) : _parent(parent) {}
        handle_type(const handle_type&)                      = delete;
        handle_type& operator=(const handle_type&)           = delete;
        handle_type(handle_type&& other) noexcept            = default;
        handle_type& operator=(handle_type&& other) noexcept = default;
        ~handle_type()                                       = default;

        parent_type& _parent;

      public:
        template <class... Args>
        inline T* create_pointer(Args&&... arg) const;

        inline T*   protect(const atomic_pointer_type& aptr) const;
        inline void safe_delete(pointer_type ptr);

        inline void protect_raw(pointer_type ptr) const;
        inline void delete_raw(pointer_type ptr);
        inline bool is_safe(pointer_type ptr) const;

        inline void       unprotect(pointer_type ptr) const;
        inline void       unprotect(std::vector<pointer_type>& vec) const;
        inline guard_type guard(const atomic_pointer_type& aptr);
        inline guard_type guard(pointer_type ptr);

        void print() const;
    };

    [[no_unique_address]] destructor_type _destructor;
    [[no_unique_address]] allocator_type  _allocator;

    void delete_raw(pointer_type ptr)
    {
        auto cptr = mark::clear(ptr);
        alloc_traits::destroy(_allocator, cptr);
        alloc_traits::deallocate(_allocator, cptr, 1);
    }
    handle_type get_handle() { return handle_type(_allocator); }
};





template <class T, class D, class A>
template <class... Args>
T* sequential_manager<T, D, A>::handle_type::create_pointer(
    Args&&... args) const
{
    // return new T(std::forward<Args>(arg)...);
    auto temp = alloc_traits::allocate(_parent._allocator, 1);
    alloc_traits::construct(_parent._allocator, temp,
                            std::forward<Args>(args)...);
    return temp; // new T(std::forward<Args>(arg)...);
}

template <class T, class D, class A>
T* sequential_manager<T, D, A>::handle_type::protect(
    const atomic_pointer_type& aptr) const
{
    return aptr.load();
}

template <class T, class D, class A>
void sequential_manager<T, D, A>::handle_type::safe_delete(pointer_type ptr)
{
    // auto cptr = mark::clear(ptr);
    // alloc_traits::destroy(_allocator, cptr);
    // alloc_traits::deallocate(_allocator, cptr, 1);

    _parent._destructor.destroy(*this, mark::clear(ptr));

    // delete mark::clear(ptr);
}


template <class T, class D, class A>
void sequential_manager<T, D, A>::handle_type::protect_raw(pointer_type) const
{
    return;
}

template <class T, class D, class A>
void sequential_manager<T, D, A>::handle_type::delete_raw(pointer_type ptr)
{
    _parent.delete_raw(ptr);
}

template <class T, class D, class A>
bool sequential_manager<T, D, A>::handle_type::is_safe(pointer_type) const
{
    return false;
}


template <class T, class D, class A>
void sequential_manager<T, D, A>::handle_type::unprotect(pointer_type) const
{
    return;
}

template <class T, class D, class A>
void sequential_manager<T, D, A>::handle_type::unprotect(
    std::vector<pointer_type>&) const
{
    return;
}


template <class T, class D, class A>
typename sequential_manager<T, D, A>::handle_type::guard_type
sequential_manager<T, D, A>::handle_type::guard(const atomic_pointer_type& aptr)
{
    return guard_type(*this, aptr);
}

template <class T, class D, class A>
typename sequential_manager<T, D, A>::handle_type::guard_type
sequential_manager<T, D, A>::handle_type::guard(pointer_type ptr)
{
    return guard_type(*this, ptr);
}


template <class T, class D, class A>
void sequential_manager<T, D, A>::handle_type::print() const
{
    out_tm::out() << "* print sequential reclamation handle *" << std::endl;
    return;
}

} // namespace reclamation_tm
} // namespace utils_tm
