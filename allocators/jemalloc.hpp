#pragma once

#include <algorithm>
#include <cstddef>
#include <type_traits>

#define JEMALLOC_NO_RENAME
#include <jemalloc/jemalloc.h>



// implementation based on StackExchange post
// [[https://codereview.stackexchange.com/questions/217488/a-c17-stdallocator-implementation]]
// An implementation of C++17 std::allocator with deprecated members omitted

namespace utils_tm
{
namespace allocators_tm
{

template <class T>
class jeallocator
{
  public:
    using value_type                             = T;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal                        = std::true_type;

    jeallocator() noexcept                   = default;
    jeallocator(const jeallocator&) noexcept = default;
    ~jeallocator()                           = default;

    template <class U>
    jeallocator(const jeallocator<U>&) noexcept
    {
    }

    T* allocate(std::size_t n)
    {
        return static_cast<T*>(je_malloc(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t) //
    {
        return je_free(p);
    }
};

template <class T, class U>
bool operator==(const jeallocator<T>&, const jeallocator<U>&) noexcept
{
    return true;
}

template <class T, class U>
bool operator!=(const jeallocator<T>&, const jeallocator<U>&) noexcept
{
    return false;
}





// the default alignment is two cache lines
static constexpr size_t aligned_jeallocator_default_alignment = 128;

template <class T, size_t Alignment = aligned_jeallocator_default_alignment>
class aligned_jeallocator
{
  private:
    static constexpr size_t alignment = std::max(Alignment, sizeof(T));

  public:
    using value_type                             = T;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal                        = std::true_type;

    aligned_jeallocator() noexcept                           = default;
    aligned_jeallocator(const aligned_jeallocator&) noexcept = default;
    ~aligned_jeallocator()                                   = default;

    template <class U, size_t Ua = aligned_jeallocator_default_alignment>
    aligned_jeallocator(const aligned_jeallocator<U, Ua>&) noexcept
    {
    }

    T* allocate(std::size_t n)
    {
        return static_cast<T*>(je_aligned_alloc(alignment, n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t) //
    {
        return je_free(p);
    }
};

template <class T, class U, size_t Ta, size_t Ua>
bool operator==(const aligned_jeallocator<T, Ta>&,
                const aligned_jeallocator<U, Ua>&) noexcept
{
    return true;
}

template <class T, class U, size_t Ta, size_t Ua>
bool operator!=(const aligned_jeallocator<T, Ta>&,
                const aligned_jeallocator<U, Ua>&) noexcept
{
    return false;
}


} // namespace allocators_tm
} // namespace utils_tm
