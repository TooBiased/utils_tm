#pragma once

#include <iostream>
#include <fstream>

namespace out_tm
{

// DEFINES THE OUTPUT TYPE THE *************************************************
// DEVICE CAN BE CHANGED FROM TERMINAL TO FILE AND BACK ************************
class output_type
{
public:
    std::ostream* output;

public:
    output_type () : is_fstream(0)
    {
        output = &(std::cout);
    }

    void set_terminal()
    {
        cleanup();
        is_fstream = false;
        output = &(std::cout);
    }

    void set_file(std::string& name)
    {
        cleanup();

        std::ofstream* fout = new std::ofstream(name, std::ofstream::out |
                                                      std::ofstream::app);
        if (! fout->is_open())
        {
            std::cerr << "fstream is not open" << std::endl;
            set_terminal();
            return;
        }

        is_fstream = true;
        output     = fout;
    }

    void disable()
    {
        cleanup();
        std::ofstream* fout = new std::ofstream(0);
        if (fout->is_open())
        {
            std::cerr << "disabled stream is open" << std::endl;
        }

        is_fstream = true;
        output     = fout;
    }

    template<class T>
    friend output_type& operator<<(output_type& out, T&& t);

private:
    bool is_fstream;

    void cleanup()
    {
        if (is_fstream)
        {
            std::ofstream* fout = static_cast<std::ofstream*>(output);
            if (fout->is_open()) fout->close();
            delete fout;
        }
    }
};

// THE STATIC OUTPUT OBJECT (THIS REPLACES std::cout) **************************
output_type& out()
{
    static output_type static_out;
    return static_out;
}


enum class color
{
    reset    = 0,
    black    = 30,
    red      = 31,
    green    = 32,
    yellow   = 33,
    blue     = 34,
    magenta  = 35,
    cyan     = 36,
    white    = 37,
    bblack   = 40,
    bred     = 41,
    bgreen   = 42,
    byellow  = 43,
    bblue    = 44,
    bmagenta = 45,
    bcyan    = 46,
    bwhite   = 47
};


// DIFFERENT OVERLOADS OF THE << OPERATOR **************************************

// base case, everything is output through the device
template<typename T>
inline output_type& operator<<(output_type& cons, T&& t)
{
    *(cons.output) << std::forward<T>(t);
    return cons;
}

// necessary for endl and flush
using omanip_t = std::ostream& (*) (std::ostream &);
inline output_type& operator<<(output_type& cons, omanip_t t)
{
    *(cons.output) << t;
    return cons;
}

// controls the color of the output
inline output_type& operator<<(output_type& cons, color c)
{
    int ccode = static_cast<int>(c);
    if (ccode >= 40) *(cons.output) << "\033[1;" << ccode-10 << "m";
    else             *(cons.output) << "\033[0;" << ccode    << "m";
    return cons;
}

// controls the width of the output
class w { public: int _w; w(int w) : _w(w) { } };
inline output_type& operator<<(output_type& cons, w w)
{
    cons.output->width(w._w);
    return cons;
}


};
