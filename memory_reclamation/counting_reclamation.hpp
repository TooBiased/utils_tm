#pragma once

#include <atomic>
#include <tuple>
#include <vector>
#include <mutex>

#include "../data_structures/circular_buffer.hpp"
#include "../mark_pointer.hpp"
#include "../output.hpp"
#include "../debug.hpp"

#include "default_destructor.hpp"
#include "reclamation_guard.hpp"

namespace utils_tm
{
namespace reclamation_tm
{
    template <class T>
    class counted_object : public T
    {
    public:
        template <class ... Args>
        counted_object(Args&& ... arg)
            : T(std::forward<Args>(arg)...), counter(0), epoch(0)
        { }

        void erase()
        { this->T::~T(); }

        template <class ... Args>
        void emplace(Args&& ... arg)
        { new (this) T(std::forward<Args>(arg)...); }

        std::atomic_uint counter;
        uint epoch;
        static constexpr uint mark = 1<<31;
    };



    template<class T,
             class Destructor = default_destructor<T>,
             template <class> class Queue = circular_buffer,
             class CObj = counted_object<T>>
    class counting_manager
    {
    public:


    private:
        using this_type           = counting_manager<T,Destructor,Queue>;
        using destructor_type     = Destructor;
        using internal_type       = CObj;
        using queue_type          = Queue<internal_type*>;
    public:
        using pointer_type        = T*;
        using atomic_pointer_type = std::atomic<T*>;

        counting_manager(Destructor&& destructor = Destructor())
            : _destructor(std::move(destructor)) {}
        counting_manager(const counting_manager&) = delete;
        counting_manager& operator=(const counting_manager&) = delete;
        counting_manager(counting_manager&& other);
        counting_manager& operator=(counting_manager&& other);
        ~counting_manager() = default;

        class handle_type
        {
        private:
            using parent_type     = counting_manager<T, Destructor, Queue>;
            using this_type       = handle_type;
            using internal_type   = typename parent_type::internal_type;

        public:
            using pointer_type        = typename parent_type::pointer_type;
            using atomic_pointer_type = typename parent_type::atomic_pointer_type;
            using guard_type          = reclamation_guard<T, this_type>;

            handle_type(parent_type& parent) : n(0), _parent(parent) { }
            handle_type(const handle_type&) = delete;
            handle_type& operator=(const handle_type&) = delete;
            handle_type(handle_type&& other) noexcept = default;
            handle_type& operator=(handle_type&& other) noexcept = default;
            ~handle_type() = default;

            size_t       n;
        private:
            parent_type& _parent;

        public:
            template <class ...Args>
            inline T*   create_pointer(Args&& ...arg) const;

            inline T*   protect(atomic_pointer_type& ptr);
            inline void protect_raw(pointer_type ptr);
            inline void unprotect(pointer_type ptr);
            inline void unprotect(std::vector<pointer_type>& vec);
            inline guard_type guard(atomic_pointer_type& ptr);
            inline guard_type guard(pointer_type ptr);

            inline void safe_delete(pointer_type ptr);
            inline void delete_raw(pointer_type ptr);
            inline bool is_safe(pointer_type ptr);

            void print() const;
        private:
            inline void increment_counter(pointer_type ptr) const;
            inline void decrement_counter(pointer_type ptr);
            inline void mark_counter(pointer_type ptr);
            inline void internal_delete(internal_type* iptr);

        };
        handle_type get_handle() { return handle_type(*this); }

    private:
        destructor_type _destructor;
        std::mutex      _freelist_mutex;
        queue_type      _freelist;
    };


    template<class T, class D, template <class> class Q, class CO>
    counting_manager<T,D,Q,CO>::counting_manager(counting_manager&& other)
        : _destructor(std::move(other._destructor)), _freelist_mutex(), _freelist()
    {
        std::lock_guard<std::mutex> guard(other._freelist_mutex);
        _freelist = std::move(other._freelist);
    }

    template<class T, class D, template <class> class Q, class CO>
    counting_manager<T,D,Q,CO>&
    counting_manager<T,D,Q,CO>::operator=(counting_manager&& other)
    {
        if (&other == this) return *this;
        this->~counting_manager();
        new (this) counting_manager(std::move(other));
        return *this;
    }



    // *** HANDLE STUFF ********************************************************
    // ***** HANDLE MAIN FUNCTIONALITY *****************************************
    template<class T, class D, template <class> class Q, class CO> template <class ...Args>
    T* counting_manager<T,D,Q,CO>::handle_type::create_pointer(Args&& ... arg) const
    {
        #ifndef NO_FREELIST
        internal_type* temp = nullptr;
        {   // area for the lock_guard
            std::lock_guard<std::mutex> guard(_parent._freelist_mutex);
            auto result = _parent._freelist.pop_front();
            if (result)
            {
                temp = result.value();
            }
        }
        if (temp)
        {
            temp->emplace(std::forward<Args>(arg)...);
            return temp;
        }
        #endif
        return new internal_type(std::forward<Args>(arg)...);
    }

    template<class T, class D, template <class> class Q, class CO>
    T* counting_manager<T,D,Q,CO>::handle_type::protect(atomic_pointer_type& ptr)
    {
        ++n;
        auto temp  = ptr.load();
        increment_counter(temp);
        auto temp2 = ptr.load();
        while (temp != temp2)
        {
            decrement_counter(temp);
            temp = temp2;
            increment_counter(temp);
            temp2 = ptr.load();
        }
        return temp;
    }

    template<class T, class D, template <class> class Q, class CO>
    void counting_manager<T,D,Q,CO>::handle_type::protect_raw(pointer_type ptr)
    {
        ++n;
        increment_counter(ptr);
    }

    template<class T, class D, template <class> class Q, class CO>
    void counting_manager<T,D,Q,CO>::handle_type::unprotect(pointer_type ptr)
    {
        --n;
        decrement_counter(ptr);
    }

    template<class T, class D, template <class> class Q, class CO>
    void
    counting_manager<T,D,Q,CO>::handle_type::unprotect(std::vector<pointer_type>& vec)
    {
        n -= vec.size();
        for (auto ptr : vec)
            decrement_counter(ptr);
    }

    template<class T, class D, template <class> class Q, class CO>
    typename counting_manager<T,D,Q,CO>::handle_type::guard_type
    counting_manager<T,D,Q,CO>::handle_type::guard(atomic_pointer_type& aptr)
    {
        return make_rec_guard(*this, aptr);
    }

    template<class T, class D, template <class> class Q, class CO>
    typename counting_manager<T,D,Q,CO>::handle_type::guard_type
    counting_manager<T,D,Q,CO>::handle_type::guard(pointer_type ptr)
    {
        return make_rec_guard(*this, ptr);
    }

    template<class T, class D, template <class> class Q, class CO>
    void counting_manager<T,D,Q,CO>::handle_type::safe_delete(pointer_type ptr)
    {
        mark_counter(ptr);
    }

    template<class T, class D, template <class> class Q, class CO>
    void counting_manager<T,D,Q,CO>::handle_type::delete_raw(pointer_type ptr)
    {
        auto iptr = static_cast<internal_type*>(mark::clear(ptr));

        iptr->internal_type::erase();
        iptr->epoch++;

        std::lock_guard<std::mutex> guard(_parent._freelist_mutex);
        _parent._freelist.push_back(iptr);
    }

    template<class T, class D, template <class> class Q, class CO>
    bool counting_manager<T,D,Q,CO>::handle_type::is_safe(pointer_type ptr)
    {
        internal_type* iptr = static_cast<internal_type*>(mark::clear(ptr));
        return !iptr->counter.load();
    }




    // ***** HANDLE HELPER FUNCTIONS
    template<class T, class D, template <class> class Q, class CO>
    void counting_manager<T,D,Q,CO>::handle_type::print() const
    {
        std::lock_guard<std::mutex> guard(_parent._freelist_mutex);
        out_tm::out() << "* print in counting reclamation strategy "
                      << _parent._freelist.size() << " elements in the freelist *"
                      << std::endl;
    }

    template<class T, class D, template <class> class Q, class CO>
    void counting_manager<T,D,Q,CO>::handle_type::increment_counter(pointer_type ptr) const
    {
        static_cast<internal_type*>(mark::clear(ptr))->counter.fetch_add(1);
    }

    template<class T, class D, template <class> class Q, class CO>
    void counting_manager<T,D,Q,CO>::handle_type::decrement_counter(pointer_type ptr)
    {

        internal_type* iptr = static_cast<internal_type*>(mark::clear(ptr));
        auto           temp = iptr->counter.fetch_sub(1);
        debug_tm::if_debug("Warning: in decrement_counter - "
                           "created a negative counter", temp == 0);
        debug_tm::if_debug("Warning: in decrement counter - "
                           "weird counter",
                           (temp>666) && (temp<internal_type::mark+1));

        if (temp == internal_type::mark+1)
        {
            internal_delete(iptr);
        }
    }

    template<class T, class D, template <class> class Q, class CO>
    void counting_manager<T,D,Q,CO>::handle_type::mark_counter(pointer_type ptr)
    {
        internal_type* iptr = static_cast<internal_type*>(mark::clear(ptr));

        auto temp = iptr->counter.fetch_or(internal_type::mark);
        if (temp & internal_type::mark)
        {
            debug_tm::if_debug_critical("Warning: in counting pointer trying to mark a marked pointer");
        }

        if (temp == 0) // element was unused, and not marked before
        {
            internal_delete(iptr);
        }
    }

    template<class T, class D, template <class> class Q, class CO>
    void
    counting_manager<T,D,Q,CO>::handle_type::internal_delete(internal_type* ptr)
    {

        auto temp = internal_type::mark;
        if (ptr->counter.compare_exchange_strong(temp,0))
        {
            _parent._destructor.destroy(*this,
                                        static_cast<pointer_type>(ptr));
        }

    }
};
};
