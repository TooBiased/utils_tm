#pragma once

#include "../memory_reclamation/delayed_reclamation.hpp"

namespace utils_tm
{
    template <class T>
    class concurrent_singly_linked_list
    {
    private:
        using this_type = concurrent_singly_linked_list<T>;

    public:
        class queue_item_type
        {
        public:
            queue_item_type(const T& element) : value(element), next(nullptr) { }
            T                             value;
            std::atomic<queue_item_type*> next;
        };

        using value_type     = T;
        using reference_type = T&;
        using pointer_type   = T*;

        concurrent_singly_linked_list() : _head(nullptr) { }

    private:
        std::atomic<queue_item_type*> _head;

    public:
        template <class rec_handle>
        inline void insert(rec_handle& h, const T& element);

        template <class rec_handle>
        inline void insert(rec_handle&, queue_item_type* item);

        template <class rec_handle>
        inline bool remove(rec_handle& h, const T& element);


        template <class rec_handle>
        inline queue_item_type* find(rec_handle& h, const T& element);
    };





    template <class T> template <class rec_handle>
    void
    concurrent_singly_linked_list<T>::insert(rec_handle& h, const T& element)
    {
        queue_item_type* item = new queue_item_type{element};
        insert(h, item);
    }

    template <class T> template <class rec_handle>
    void
    concurrent_singly_linked_list<T>::insert(rec_handle&, queue_item_type* item)
    {
        auto first = _head.load();
        item->next.store(first);
        do
        {
            item->next.store(first);
        }
        while (!_head.compare_exchange(first, item));
    }

    template <class T> template <class rec_handle>
    bool
    concurrent_singly_linked_list<T>::remove(rec_handle& h, const T& element)
    {

    }

    template <class T> template <class rec_handle>
    typename concurrent_singly_linked_list<T>::queue_item_type*
    concurrent_singly_linked_list<T>::find(rec_handle& h, const T& element)
    {
        auto current = h.protect(_head);
        while (current)
        {
            if (element == current->value)
            {
                return current;
            }

            auto next = h.protect(current->next);
            h.unprotect(current);
            if (current == next)
            {
                h.unprotect(next);
                next = h.protect(_head);
            }
            current = next;
        }
        // there could be a protected nullptr?
        return nullptr;
    }
};
