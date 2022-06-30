#pragma once

#include <algorithm>
#include <cstddef>
#include <malloc.h>
#include <type_traits>


// implementation based on StackExchange post
// [[https://codereview.stackexchange.com/questions/217488/a-c17-stdallocator-implementation]]
// An implementation of C++17 std::allocator with deprecated members omitted

namespace utils_tm
{
namespace allocators_tm
{

// the default alignment is two cache lines
static constexpr size_t aligned_allocator_default_alignment = 128;

template <class T, size_t Alignment = aligned_allocator_default_alignment>
class aligned_allocator
{
  private:
    static constexpr size_t alignment = std::max(Alignment, sizeof(T));

  public:
    using value_type                             = T;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal                        = std::true_type;

    aligned_allocator() noexcept                         = default;
    aligned_allocator(const aligned_allocator&) noexcept = default;
    ~aligned_allocator()                                 = default;

    template <class U, size_t Ua = aligned_allocator_default_alignment>
    aligned_allocator(const aligned_allocator<U, Ua>&) noexcept
    {
    }

    T* allocate(std::size_t n)
    {
        return static_cast<T*>(aligned_alloc(alignment, n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t) //
    {
        return free(p);
    }
};

template <class T, class U, size_t Ta, size_t Ua>
bool operator==(const aligned_allocator<T, Ta>&,
                const aligned_allocator<U, Ua>&) noexcept
{
    return true;
}

template <class T, class U, size_t Ta, size_t Ua>
bool operator!=(const aligned_allocator<T, Ta>&,
                const aligned_allocator<U, Ua>&) noexcept
{
    return false;
}

} // namespace allocators_tm
} // namespace utils_tm
