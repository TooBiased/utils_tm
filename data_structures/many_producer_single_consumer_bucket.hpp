#include <atomic>
#include <algorithm>
#include <cstddef>


// This structure is owned by one thread, and is meant to pass inputs
// to that thread
template <class T>
class many_producer_single_consumer_buffer
{
private:
    using this_type = many_producer_single_consumer_buffer<T>;

    bool                         _first;
    size_t                       _capacity;
    std::atomic<std::atomic<T>*> _pos;
    T*                           _buffer[2];

public:
    using value_type = T;

    many_producer_single_consumer_buffer(size_t capacity) : _first(true), _capacity(capacity)
    {
        _buffer[0] = new T[capacity];
        _buffer[1] = new T[capacity];
        if (_buffer[0] > _buffer[1]) std::swap(_buffer[0], _buffer[1]);

        std::fill(_buffer[0], _buffer[0] + capacity, T());
        std::fill(_buffer[1], _buffer[1] + capacity, T());

        _pos.store(_buffer[0]);
    }

    many_producer_single_consumer_buffer(const many_producer_single_consumer_buffer&) = delete;
    many_producer_single_consumer_buffer& operator=(const many_producer_single_consumer_buffer&) = delete;
    many_producer_single_consumer_buffer(many_producer_single_consumer_buffer&& other)
        : _first(other.first),
          _capacity(other._capacity),
          _pos(other._pos.load())
    {
        _buffer[0] = other._buffer[0];
        _buffer[1] = other._buffer[1];
        other._buffer[0] = nullptr;
        other._buffer[1] = nullptr;
    }
    many_producer_single_consumer_buffer& operator=(many_producer_single_consumer_buffer&& rhs)
    {
        if (&rhs == this) return *this;

        this->~this_type();
        new (this) many_producer_single_consumer_buffer(rhs);
        return *this;
    }
    ~many_producer_single_consumer_buffer()
    {
        delete[] _buffer[0];
        delete[] _buffer[1];
    }


    // can be called concurrently by all kinds of threads
    // returns false if the buffer is full
    bool push_back(const T& e)
    {
        auto tpos = _pos.fetch_add(1);
        if (tpos >= _capacity) return false;

        tpos->store(e);
    }

    // can be called concurrent to push_backs but only by the owning thread
    // pull_all breaks all previously pulled elements
    std::pair<std::atomic<T>*, std::atomic<T>*> pull_all()
    {
        auto current_buffer = _buffer[(_first) ? 0 : 1];
        auto next_buffer    = _buffer[(_first) ? 1 : 0];
        std::fill(next_buffer, next_buffer+_capacity, T());

        auto end = _pos.exchange(next_buffer);
        end = std::min(end, current_buffer+_capacity);

        _first = !_first;
        return std::make_pair(current_buffer, end);
    }
};
