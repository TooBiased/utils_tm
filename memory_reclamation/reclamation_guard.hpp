#pragma once

#include <atomic>
#include "../mark_pointer.hpp"

namespace utils_tm
{
namespace reclamation_tm
{

    template <class T, class ReclamationType>
    class reclamation_guard
    {
    private:
        using this_type           = reclamation_guard<T,ReclamationType>;
        using reclamation_type    = ReclamationType;
    public:
        using pointer_type        = typename ReclamationType::pointer_type;
        using atomic_pointer_type = typename ReclamationType::atomic_pointer_type;

        inline reclamation_guard(reclamation_type& rec, atomic_pointer_type& aptr);
        inline reclamation_guard(reclamation_type& rec, pointer_type ptr);

        inline reclamation_guard(const reclamation_guard&) = delete;
        inline reclamation_guard& operator=(const reclamation_guard&) = delete;

        inline reclamation_guard(reclamation_guard&& source);
        inline reclamation_guard& operator=(reclamation_guard&& source);
        inline ~reclamation_guard();

        inline    operator T*()   const { return _ptr; }
        inline    operator bool() const { return _ptr != nullptr; }
        inline T* operator ->()   const { return _ptr; }
        inline T& operator * ()   const { return *_ptr; }
    private:
        reclamation_type& _rec_handle;
        T*                _ptr;
    };


template <class T, class R>
reclamation_guard<T,R>::reclamation_guard(reclamation_type& rec,
                                          atomic_pointer_type& aptr)
    : _rec_handle(rec), _ptr(rec.protect(aptr)) { }


template <class T, class R>
reclamation_guard<T,R>::reclamation_guard(reclamation_type& rec,
                                          pointer_type ptr)
    : _rec_handle(rec), _ptr(ptr) { _rec_handle.protect_raw(ptr); }

template <class T, class R>
reclamation_guard<T,R>::reclamation_guard(reclamation_guard&& source)
    : _rec_handle(source._rec_handle), _ptr(source._ptr)
{
    source._ptr = nullptr;
}

template <class T, class R>
reclamation_guard<T,R>&
reclamation_guard<T,R>::operator=(reclamation_guard&& source)
{
    if (&source == this) return *this;
    this->~reclamation_guard();
    new (this) reclamation_guard(std::move(source));
    return *this;
}

template <class T, class R>
reclamation_guard<T,R>::~reclamation_guard()
{
    if (_ptr)
        _rec_handle.unprotect(_ptr);
}






    template<class T, class ReclamationType>
    reclamation_guard<T,ReclamationType>
    make_rec_guard(ReclamationType& rec, T* ptr)
    {
        return reclamation_guard<T,ReclamationType>(rec, ptr);
    }

    template<class T, class ReclamationType>
    reclamation_guard<T,ReclamationType>
    make_rec_guard(ReclamationType& rec, std::atomic<T*>& aptr)
    {
        return reclamation_guard<T,ReclamationType>(rec, aptr);
    }

};
};
