#include <string>
#include <random>

#include "output.hpp"
#include "command_line_parser.hpp"

#include "data_structures/circular_buffer.hpp"

namespace utm = utils_tm;
namespace otm = utils_tm::out_tm;

class move_checker
{
public:
    size_t nmbr;

    move_checker(size_t i=0) : nmbr(i) {}
    move_checker(const move_checker&) = delete;
    move_checker& operator=(const move_checker&) = delete;
    move_checker(move_checker&&) = default;
    move_checker& operator=(move_checker&&) = default;

    bool operator==(const move_checker& other)
    { return other.nmbr == nmbr; }
};

void generate_random(size_t n, std::vector<size_t>& container)
{
    std::mt19937_64 re;
    std::uniform_int_distribution<size_t> dis;

    for (size_t i = 0; i < n; ++i)
    {
        container.push_back(dis(re));
    }
}

template <class T>
void run_test(size_t n, size_t c)
{
    std::vector<size_t> input;
    input.reserve(n);
    generate_random(n, input);

    circular_buffer<T> container{c};

    for (size_t i = 0; i < input.size(); ++i)
    {
        container.emplace_back(input[i]);
    }
}


int main(int argn, char** argc)
{
    utm::command_line_parser cline{argn, argc};
    size_t n = cline.int_arg("-n", 10000);
    size_t c = cline.int_arg("-c", 1000);

    if (! cline.report()) return 1;

    otm::out() << otm::color::byellow << "START CORRECTNESS TEST"
               << otm::color::reset << std::endl;
    otm::out() << "testing: circular_buffer" << std::endl;


    otm::out() << "Elements are pushed and popped from the buffer." << std::endl
               << "First we test size_t elements then std::string elements:"
               << std::endl
               << otm::color::blue
               << "  1. randomly generate keys"  << std::endl
               << "  2. push_front and pop_back" << std::endl
               << "  3. push_back and pop_front" << std::endl
               << otm::color::reset << std::endl;


    otm::out() << otm::color::bgreen << "START TEST with <size_t>"
               << otm::color::reset << std::endl;

    run_test<size_t>(n, c);

    // otm::out() << otm::color::bgreen << "START TEST with <std::string>"
    //            << otm::color::reset << std::endl;

    // run_test<std::string>(n, c);

    otm::out() << otm::color::bgreen << "START TEST with <move_checker>"
               << otm::color::reset << std::endl;

    run_test<move_checker>(n, c);

    otm::out() << otm::color::bgreen << "END CORRECTNESS TEST"
               << otm::color::reset << std::endl;

    return 0;
}
