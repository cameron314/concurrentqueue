#pragma once

#include <utility>
#include "dlib/pipe.h"
#include "wrappers.h"

template<typename T>
struct DlibQueueWrapper
{
public:
    typedef DummyToken producer_token_t;
    typedef DummyToken consumer_token_t;

public:
    DlibQueueWrapper() : q(1024*1024*1024) { }

    template<typename U>
    inline bool enqueue(U&& item)
    {
        return q.enqueue(std::forward<U>(item));
    }

    inline bool try_dequeue(T& item)
    {
        return q.dequeue_or_timeout(item, 0);
    }

    // Dummy token methods (not used)
    bool enqueue(producer_token_t const&, T const&) { return false; }
    bool try_enqueue(producer_token_t, T const&) { return false; }
    bool try_dequeue(consumer_token_t, T& item) { return false; }
    template<typename It> bool enqueue_bulk(It, size_t) { return false; }
    template<typename It> bool enqueue_bulk(producer_token_t const&, It, size_t) { return false; }
    template<typename It> size_t try_dequeue_bulk(It, size_t) { return 0; }
    template<typename It> size_t try_dequeue_bulk(consumer_token_t, It, size_t) { return 0; }

private:
    dlib::pipe<T> q;
};
