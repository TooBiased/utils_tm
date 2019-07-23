#pragma once

#include <cstddef>
#include <sstream>

template <class int_type>
std::string bit_print (int_type t)
{
    std::ostringstream buffer;

    constexpr size_t length = sizeof(int_type)*8;
    size_t temp = 1ull << (length-1);

    size_t i = 0;
    while (temp > 0)
    {
        buffer << ((temp & t) ? 1 : 0);
        if (i++ >= 7) { i=0; buffer << " "; }
        temp >>= 1;
    }

    return buffer.str();
}
