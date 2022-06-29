#include <algorithm>
#include <atomic>
#include <iostream>
#include <memory>
#include <sstream>
#include <tuple>

#include "command_line_parser.hpp"
#include "pin_thread.hpp"
#include "thread_coordination.hpp"

namespace utm = utils_tm;
namespace otm = utm::out_tm;
namespace ttm = utm::thread_tm;
using c       = otm::color;


#include "allocators/aligned_alloc.hpp"

#ifndef NO_TBB
#define TBB_PREVIEW_MEMORY_POOL 1
#include "allocators/tbb_pool_alloc.hpp"
#include "tbb/memory_pool.h"
#endif

#ifndef NO_JEMALLOC
#include "allocators/jemalloc.hpp"
#endif

namespace atm = utm::allocators_tm;

using value_type = size_t;


void print_help()
{
    otm::out() << "This is a test for our memory allocators\n"
               << c::magenta + "* Executable\n"
               << "   tests/src/test_allocators.cpp.\n"
               << c::magenta + "* Test subjects\n"
               << "   "
               << c::green +
                      "aligned_allocator, jemallocator, tbb_pool_allocator, "
                      "std::allocator, tbb::scalable_allocator\n"
               << "   from " << c::yellow + "allocators/"
               << "\n"
               << c::magenta + "* Process\n"
               << "   For each tested allocator, p threads repeatedly\n"
               << "   1. allocate 1 element\n"
               << "   2. deallocate one of the previously allocated ones\n"
                  "      in a random order and allocate a new one\n"
               << "   3. deallocate all currently allocated elements\n"
               << c::magenta + "* Parameters\n"
               << "   -p #(threads)\n"
               << "   -n #(number of operations per stage)\n"
               << "   -it #(repeats of the test)\n"
               << c::magenta + "* Outputs\n"
               << "   i            counts the repeats\n"
               << "   t_allocate   time for step one\n"
               << "   t_mixed      time for step two\n"
               << "   t_deallocate time for step three\n"
               << std::flush;
}



static std::atomic_size_t current = 0;
static value_type**       ptrs;


template <class AllocatorType, class ThreadType>
struct test
{
    static int execute(ThreadType thrd, size_t it, size_t n)
    {
        utm::pin_to_core(thrd.id);
        AllocatorType allocator;
        using alloc_traits = std::allocator_traits<AllocatorType>;

        for (size_t i = 0; i < it; ++i)
        {
            auto duration0 = thrd.synchronized([n, &allocator]() {
                ttm::execute_parallel(current, n, [&allocator](int j) {
                    ptrs[j] = alloc_traits::allocate(allocator, 1);
                    alloc_traits::construct(allocator, ptrs[j]);
                });
                return 0;
            });
            if (thrd.is_main)
            {
                current.store(0);
                std::random_shuffle(ptrs, ptrs + n);
            }
            auto duration1 = thrd.synchronized([n, &allocator]() {
                ttm::execute_parallel(current, n, [&allocator](int j) {
                    alloc_traits::destroy(allocator, ptrs[j]);
                    alloc_traits::deallocate(allocator, ptrs[j], 1);
                    ptrs[j] = alloc_traits::allocate(allocator, 1);
                    alloc_traits::construct(allocator, ptrs[j]);
                });
                return 0;
            });
            if (thrd.is_main)
            {
                current.store(0);
                std::random_shuffle(ptrs, ptrs + n);
            }
            auto duration2 = thrd.synchronized([n, &allocator]() {
                ttm::execute_parallel(current, n, [&allocator](int j) {
                    alloc_traits::destroy(allocator, ptrs[j]);
                    alloc_traits::deallocate(allocator, ptrs[j], 1);
                });
                return 0;
            });

            thrd.out << otm::width(3) + i
                     << otm::width(12) + (duration0.second / 1000000.)
                     << otm::width(12) + (duration1.second / 1000000.)
                     << otm::width(12) + (duration2.second / 1000000.)
                     << std::endl;

            if (thrd.is_main) { current.store(0); }
        }
        return 0;
    }
};





template <class ThreadType>
using std_test = test<std::allocator<value_type>, ThreadType>;
template <class ThreadType>
using aligned_test =
    test<atm::aligned_allocator<value_type>, ThreadType>; // default alignment
template <class ThreadType>
using small_aligned_test = test<atm::aligned_allocator<value_type, 8>,
                                ThreadType>; // smaller alignment

#ifndef NO_TBB
template <class ThreadType>
using tbb_scalable_test = test<tbb::scalable_allocator<value_type>, ThreadType>;
template <class ThreadType>
using tbb_pool_test = test<atm::tbb_pool_allocator<value_type>, ThreadType>;
#endif

#ifndef NO_JEMALLOC
template <class ThreadType>
using je_test = test<atm::jeallocator<value_type>, ThreadType>;
template <class ThreadType>
using je_aligned_test = test<atm::aligned_jeallocator<value_type>, ThreadType>;
template <class ThreadType>
using small_je_aligned_test =
    test<atm::aligned_jeallocator<value_type, 8>, ThreadType>;
#endif

/* MAIN FUNCTION: READS COMMANDLINE AND STARTS TEST THREADS *******************/
int main(int argn, char** argc)
{
    utm::command_line_parser c{argn, argc};
    size_t                   p  = c.int_arg("-p", 4);
    size_t                   n  = c.int_arg("-n", 5000000);
    size_t                   it = c.int_arg("-it", 4);
    if (c.bool_arg("-h"))
    {
        print_help();
        return 0;
    }
    if (!c.report()) return 1;

    ptrs = new value_type*[n];


    otm::out() << otm::width(3) + "# it"        //
               << otm::width(12) + "t_allocate" //
               << otm::width(12) + "t_mixed"    //
               << otm::width(12) + "t_deallocate" << std::endl;

    otm::out() << otm::color::bblue + "# STD::ALLOCATOR TEST" << std::endl;
    // each thread can build their own allocator (stateless)
    //   rtm::delayed_manager<foo> delayed_mngr;
    ttm::start_threads<std_test>(p, it, n);

    otm::out() << std::endl                                      //
               << otm::color::bblue + "# ALIGNED_ALLOCATOR TEST" //
               << std::endl;
    ttm::start_threads<aligned_test>(p, it, n);

    otm::out() << std::endl                                              //
               << otm::color::bblue + "# ALIGNED_ALLOCATOR (SMALL) TEST" //
               << std::endl;
    ttm::start_threads<small_aligned_test>(p, it, n);



#ifndef NO_TBB
    otm::out() << std::endl                                            //
               << otm::color::bblue + "# TBB::SCALABLE_ALLOCATOR TEST" //
               << std::endl;
    ttm::start_threads<tbb_scalable_test>(p, it, n);

    otm::out() << std::endl                                       //
               << otm::color::bblue + "# TBB_POOL_ALLOCATOR TEST" //
               << std::endl;
    ttm::start_threads<tbb_pool_test>(p, it, n);
#endif



#ifndef NO_JEMALLOC
    otm::out() << std::endl                                //
               << otm::color::bblue + "# JEALLOCATOR TEST" //
               << std::endl;
    ttm::start_threads<je_test>(p, it, n);

    otm::out() << std::endl                                        //
               << otm::color::bblue + "# ALIGNED_JEALLOCATOR TEST" //
               << std::endl;
    ttm::start_threads<je_aligned_test>(p, it, n);

    otm::out() << std::endl                                                //
               << otm::color::bblue + "# ALIGNED_JEALLOCATOR (SMALL) TEST" //
               << std::endl;
    ttm::start_threads<small_je_aligned_test>(p, it, n);
#endif


    delete[] ptrs;

    return 0;
}
