#include <tuple>
#include <atomic>
#include <iostream>
#include <sstream>

#include "thread_coordination.hpp"
#include "pin_thread.hpp"
#include "command_line_parser.hpp"
//#include "test_coordination.hpp"

#include "memory_reclamation/counting_reclamation.hpp"
//#include "reclamation/delayed_reclamation.hpp"
#include "memory_reclamation/hazard_reclamation.hpp"
//#include "reclamation/sequential_reclamation.hpp"

namespace utm = utils_tm;
namespace otm = utm::out_tm;
namespace ttm = utm::thread_tm;
using c   = otm::color;
using w   = otm::width;


void print_help()
{
    otm::out() << "This is a test for our hazard pointer implementation\n"
               << c::magenta << "* Executable\n" << c::reset
               << "   bench/hazard_test.cpp.\n"
               << c::magenta << "* Test subject\n"
               << "   " << c::green  << "proj::hazard_tm::hazard_manager"
               << c::reset  << " from "
               << c::yellow << "impl/hazard.h"
               << c::reset  << "\n"
               << c::magenta << "* Process\n" << c::reset
               << "   Main: the main thread repeats the following it times\n"
               << "     1. wait until the others did incremented a counter\n"
               << "        (simulating some work), also wait for i-2 to be\n"
               << "        deleted (necessary for the order of the output)\n"
               << "     2. create a new foo object\n"
               << "     3. replace the current pointer with the new one\n"
               << "   Sub:  repeatedly acquire the current foo pointer and\n"
               << "         and increment its counter (in blocks of 100)\n"
               << c::magenta << "* Parameters\n" << c::reset
               << "   -p #(threads)\n"
               << "   -n #(number of increments before a pointer change) \n"
               << "   -it #(repeats of the test)\n"
               << c::magenta << "* Outputs\n" << c::reset
               << "   i          counts the repeats\n"
               << "   current    the pointer before the exchange\n"
               << "   next       the pointer after the exchange\n"
               << "   deletor    {thread id, pointer nmbr, pointer}\n"
               << std::flush;
}



static thread_local size_t thread_id;

class foo
{
public:
    foo(size_t i = 0) : id(i), counter(0) { }
    ~foo()
    {
        deleted.store(id);

        otm::buffered_out() << c::bred << "DEL    " << c::reset << w(3) << id
                            << "    ptr  " << this
                            << " deleted by " << thread_id << std::endl;
   }

    static std::atomic_int deleted;
    size_t id;
    std::atomic_size_t counter;
};
std::atomic_int foo::deleted = -1;




using reclamation_manager_type = utils_tm::reclamation_tm::hazard_manager<foo>;
using reclamation_handle_type  = typename reclamation_manager_type::handle_type;
using reclamation_atomic       = typename reclamation_manager_type::atomic_pointer_type;


alignas (64) static reclamation_manager_type recl_mngr;
alignas (64) static reclamation_atomic       the_one;
alignas (64) static std::atomic_bool         finished{false};





template <class ThreadType>
struct test;

template <>
struct test<ttm::timed_main_thread>
{
    static int execute(ttm::timed_main_thread thrd, size_t it, size_t n)
    {
        utm::pin_to_core(thrd.id);
        thread_id = thrd.id;
        auto handle = recl_mngr.get_handle();

        the_one.store(new foo(0));
        otm::buffered_out() << c::bgreen << "NEW" << c::reset
                            << "      0    start               new "
                            << the_one.load() << std::endl;

        thrd.synchronized(
            [it, n, &handle] ()
            {
                auto current = handle.protect(the_one);
                for (size_t i = 1; i <= it; ++i)
                {
                    while (current->counter.load() < n)
                    { /* wait */ }

                    auto next = handle.create_pointer(i);
                    handle.protect_raw(next);

                    //auto out = otm::buffered_out();
                    otm::buffered_out() << c::bgreen << "NEW    " << c::reset
                                        << w(3) << i
                                        << "    prev " << current
                                        << " new "     << next << std::endl;

                    if (! the_one.compare_exchange_strong(current, next))
                        otm::out() << "Error: on changing the pointer\n"
                                   << std::flush;
                    handle.unprotect(current);
                    handle.safe_delete(current);
                    current = next;
                }
                finished.store(true);
                return 0;
            });

        thrd.synchronize();
        return 0;
    }
};

template <>
struct test<ttm::untimed_sub_thread>
{
    static int execute(ttm::untimed_sub_thread thrd, size_t, size_t)
    {
        utm::pin_to_core(thrd.id);
        thread_id = thrd.id;
        auto handle = recl_mngr.get_handle();

        thrd.synchronized(
            [&handle] ()
            {
                while (!finished.load())
                {
                    auto current = handle.protect(the_one);
                    for (size_t i = 0; i < 100; ++i)
                        current->counter.fetch_add(1);
                    handle.unprotect(current);
                }
                return 0;
            });

        thrd.synchronize();
        return 0;
    }
};


/* MAIN FUNCTION: READS COMMANDLINE AND STARTS TEST THREADS *******************/
int main(int argn, char** argc)
{
    utm::command_line_parser c{argn, argc};
    size_t p   = c.int_arg("-p"   , 4);
    size_t n   = c.int_arg("-n"   , 1000);
    size_t it  = c.int_arg("-it"  , 20);
    if (c.bool_arg("-h")) { print_help(); return 0; }
    if (! c.report()) return 1;

    ttm::start_threads<test>(p, it, n);
    return 0;
}
