#pragma once

#include <atomic>
#include <tuple>
#include <vector>

#include "../mark_pointer.hpp"
#include "../output.hpp"
#include "../debug.hpp"

namespace utils_tm
{
namespace reclamation_tm
{

    namespace otm = out_tm;

    template<class T, size_t maxThreads=64, size_t maxProtections=64>
    class hazard_manager
    {
    private:
        using this_type           = hazard_manager<T>;
    public:
        using pointer_type        = T*;
        using atomic_pointer_type = std::atomic<T*>;

        hazard_manager()
            : _handle_counter(-1)
        { for (auto& a : _handles) a.store(nullptr); }
        hazard_manager(const hazard_manager&) = delete;
        hazard_manager& operator=(const hazard_manager&) = delete;
        hazard_manager(hazard_manager&& other) = delete;
        hazard_manager& operator=(hazard_manager&& other) = delete;
        ~hazard_manager();

        struct internal_handle
        {
            internal_handle() : _counter(0)
            {
                for (size_t i = 0; i < maxProtections; ++i)
                    _ptr[i].store(nullptr);
            }

            std::atomic_int               _counter;
            atomic_pointer_type           _ptr[maxProtections];
            std::atomic<internal_handle*> _free_queue_next;
        };

        class handle_type
        {
        private:
            using parent_type = hazard_manager<T,maxThreads,maxProtections>;
            using this_type   = handle_type;

        public:
            using pointer_type        = typename parent_type::pointer_type;
            using atomic_pointer_type = typename parent_type::atomic_pointer_type;

            handle_type(parent_type& parent, internal_handle& internal, int id);
            handle_type(const handle_type&) = delete;
            handle_type& operator=(const handle_type&) = delete;
            // handles should not be moved while other operations are ongoing
            handle_type(handle_type&& other) noexcept;
            handle_type& operator=(handle_type&& other) noexcept;
            ~handle_type();

            template <class ... Args>
            inline T*   create_pointer(Args&& ... args) const;

            inline T*   protect(atomic_pointer_type& ptr);
            inline void safe_delete(pointer_type ptr);

            inline void protect_raw(pointer_type ptr);
            inline void delete_raw(pointer_type ptr);
            inline bool is_safe(pointer_type ptr);

            inline void unprotect(pointer_type ptr);
            inline void unprotect(std::vector<T*>& vec);

            void print() const;

        private:
            inline void continue_deletion(pointer_type ptr, int pos);

            parent_type&     _parent;
            internal_handle& _internal;
            int              _id;
        };
        friend handle_type;

        handle_type get_handle();
        void print() const;
    private:
        std::atomic_int           _handle_counter;
        std::atomic<internal_handle*> _handles[maxThreads];

    };



    template <class T, size_t mt, size_t mp>
    hazard_manager<T,mt,mp>::~hazard_manager()
    {
        auto counter = _handle_counter.load();
        for (int i = counter; i >= 0; --i)
        {
            auto temp = _handles[i].load();
            while (!mark::get_mark<0>(temp))
            { /* wait for heandles to be destroyed */ }
        }

        for (int i = counter; i >= 0; --i)
        {
            delete mark::clear(_handles[i].load());
        }
    }

    template <class T, size_t mt, size_t mp>
    typename hazard_manager<T,mt,mp>::handle_type
    hazard_manager<T,mt,mp>::get_handle()
    {
        internal_handle* temp0 = new internal_handle();
        internal_handle* temp1;

        int i = 0;
        for (; i < int(mt); ++i)
        {
            temp1 = _handles[i].load();

            if (!temp1)
            {
                if (_handles[i].compare_exchange_strong(temp1, temp0))
                {
                    auto b = _handle_counter.load();
                    while (b < i) _handle_counter.compare_exchange_weak(b, i);
                    return handle_type(*this, *temp0, i);
                }
            }
            if (mark::get_mark<0>(temp1))
            {
                // reuse old handle
                if (_handles[i].compare_exchange_strong(temp1, mark::clear(temp1)))
                {
                    delete temp0;
                    return handle_type(*this, *mark::clear(temp1), i);
                }

            }
        }
        otm::out() << "Error: in hazard_manager get_handle -- out of bounds"
                   << std::endl;
        return handle_type(*this, *temp0, -666);
    }

    template <class T, size_t mt, size_t mp>
    void hazard_manager<T,mt,mp>::print() const
    {
        otm::out() << "hazard manager print: " << _handle_counter.load()+1 << "handles" << std::endl;
        for (size_t i = 0; i < mt; ++i)
        {
            otm::out() << i << ": " << _handles[i].load() << std::endl;
        }
    }



    template <class T, size_t mt, size_t mp>
    hazard_manager<T,mt,mp>::handle_type::handle_type(parent_type& parent,
                                                      internal_handle& internal,
                                                      int id)
        : _parent(parent), _internal(internal), _id(id)
    { }

    // There cannot be concurrent operations
    template <class T, size_t mt, size_t mp>
    hazard_manager<T,mt,mp>::handle_type::handle_type(handle_type&& source) noexcept
        : _parent(source._parent), _internal(source._internal), _id(source._id)
    {
        source._id = -1;
    }

    template <class T, size_t mt, size_t mp>
    typename hazard_manager<T,mt,mp>::handle_type&
    hazard_manager<T,mt,mp>::handle_type::operator=(handle_type&& source) noexcept
    {
        if (&source == this) return *this;
        this->handle_type::~handle_type();
        new (this) handle_type(std::move(source));
        return *this;
    }

    template <class T, size_t mt, size_t mp>
    hazard_manager<T,mt,mp>::handle_type::~handle_type()
    {
        if (_id < 0) return;

        for (int i = _internal._counter.load()-1; i >= 0; --i)
        {
            auto temp = _internal._ptr[i].exchange(nullptr);
            if (mark::get_mark<0>(temp))
                continue_deletion(mark::clear(temp), i);
        }
        _internal._counter.store(0);

        _parent._handles[_id].store(mark::mark<0>(&_internal));
    }




    template <class T, size_t mt, size_t mp> template <class ... Args>
    T* hazard_manager<T,mt,mp>::handle_type::create_pointer(Args&& ... args) const
    {
        return new T(std::forward<Args>(args)...);
    }

    template <class T, size_t mt, size_t mp>
    T* hazard_manager<T,mt,mp>::handle_type::protect(atomic_pointer_type& ptr)
    {
        auto pos = _internal._counter.fetch_add(1);
        auto temp0 = ptr.load();
        _internal._ptr[pos].store(mark::clear(temp0));
        auto temp1 = ptr.load();
        while (temp0 != temp1)
        {
            temp0 = _internal._ptr[pos].exchange(mark::clear(temp1));
            if (mark::get_mark<0>(temp0))
                continue_deletion(mark::clear(temp0), pos);

            temp0 = temp1;
            temp1 = ptr.load();
        }
        return temp0;
    }

    template <class T, size_t mt, size_t mp>
    void hazard_manager<T,mt,mp>::handle_type::safe_delete(pointer_type ptr)
    {
        auto tptr = mark::clear(ptr);
        for (int j = _parent._handle_counter.load(); j >= 0; --j)
        {
            auto temp_handle = _parent._handles[j].load();
            if (mark::get_mark<0>(temp_handle)) continue;
            for (int i = temp_handle->_counter.load()-1; i >= 0; --i)
            {
                auto temp = temp_handle->_ptr[i].load();
                if (temp == tptr)
                {
                    // transfer responsibility for deleting
                    if (temp_handle->_ptr[i]
                           .compare_exchange_strong(temp,
                                                    mark::mark<0>(tptr)))
                        // transfer successful
                        return;
                    // else concurrent unprotect we remain responsible
                }
            }
        }
        delete tptr;
    }


    template <class T, size_t mt, size_t mp>
    void hazard_manager<T,mt,mp>::handle_type::protect_raw(pointer_type ptr)
    {
        auto pos = _internal._counter.fetch_add(1);
        _internal._ptr[pos].store(mark::clear(ptr));
    }

    template <class T, size_t mt, size_t mp>
    void hazard_manager<T,mt,mp>::handle_type::delete_raw(pointer_type ptr)
    {
        delete mark::clear(ptr);
    }

    template <class T, size_t mt, size_t mp>
    bool hazard_manager<T,mt,mp>::handle_type::is_safe(pointer_type ptr)
    {
        auto tptr = mark::clear(ptr);
        for (int j = _parent._handle_counter.load(); j >= 0; --j)
        {
            auto temp_handle = _parent.handles[j].load();
            if (mark::get_mark<0>(temp_handle)) continue;
            for (int i = temp_handle->_counter.load()-1; i>=0; --i)
            {
                auto temp = temp_handle->_ptr[i].load();
                if (temp == tptr)
                    return false;
            }
        }
        return true;
    }

    template <class T, size_t mt, size_t mp>
    void hazard_manager<T,mt,mp>::handle_type::unprotect(pointer_type ptr)
    {
        auto tptr     = mark::clear(ptr);
        auto last_pos = _internal._counter.load()-1;
        auto last_ptr = mark::clear(_internal._ptr[last_pos].load());

        // handle the case, where the last pointer is unprotected
        if (tptr == last_ptr)
        {
            last_ptr = _internal._ptr[last_pos].exchange(nullptr);
            _internal._counter.store(last_pos);
            if (mark::get_mark<0>(last_ptr)) continue_deletion(tptr, last_pos);
            return;
        }

        for (int i = last_pos -1; i >= 0; --i)
        {
            auto temp = _internal._ptr[i].load();

            if (tptr == mark::clear(temp))
            {
                temp = _internal._ptr[i].exchange(last_ptr);
                if (mark::get_mark<0>(temp)) continue_deletion(tptr, i);

                temp = _internal._ptr[last_pos].exchange(nullptr);
                // if the last pointer was marked, we have to move the mark
                // protections are removed from back to front, therefore in case
                // of multiples it is not important which pointer is marked
                if (mark::get_mark<0>(temp))
                {
                    _internal._counter.store(last_pos);
                    _internal._ptr[i].store(temp);
                }
            }
        }
    }

    template <class T, size_t mt, size_t mp>
    void hazard_manager<T,mt,mp>::handle_type::unprotect(std::vector<pointer_type>& vec)
    {
        for (auto ptr : vec)
        {
            unprotect(ptr);
        }
    }


    template <class T, size_t mt, size_t mp>
    void hazard_manager<T,mt,mp>::handle_type::continue_deletion(pointer_type ptr,
                                                                 int pos)
    {
        // auto tptr = mark::clear(ptr);  // is only called with unmarked ptrs
        for (int i = pos-1; i >= 0; --i)
        {
            auto temp = _internal._ptr[i].load();
            if (temp == ptr)
            {
                // transfer responsibility for deleting (locally)
                _internal._ptr[i].store(mark::mark<0>(ptr));
                return;
            }
        }

        for (int j = _id-1; j >= 0; --j)
        {
            auto temp_handle = _parent._handles[j].load();
            if (mark::get_mark<0>(temp_handle)) continue;
            for (int i = temp_handle->_counter.load()-1; i >= 0; --i)
            {
                auto temp = temp_handle->_ptr[i].load();
                if (temp == ptr)
                {
                    // transfer responsibility for deleting
                    if (temp_handle->_ptr[i]
                           .compare_exchange_strong(temp,
                                                    mark::mark<0>(ptr)))
                        // transfer successful
                        return;
                    // else concurrent unprotect we remain responsible
                }
            }
        }
        delete ptr;
    }




    template <class T, size_t mt, size_t mp>
    void hazard_manager<T,mt,mp>::handle_type::print() const
    {
        out_tm::out() << "* print in hazard reclamation handle "
                      << _internal._counter.load()
                      << " pointer protected *"
                      << std::endl;
    }

};
};
