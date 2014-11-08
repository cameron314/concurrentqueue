#pragma once

#include <utility>

#include "tbb/concurrent_queue.h"
#include "wrappers.h"

template<typename T>
struct TbbQueueWrapper
{
public:
	typedef DummyToken producer_token_t;
	typedef DummyToken consumer_token_t;
	
public:
	template<typename U>
	inline bool enqueue(U&& item)
	{
		q.push(std::forward<U>(item));
		return true;		// assume successful allocation for the sake of the benchmarks
	}
	
	inline bool try_dequeue(T& item)
	{
		return q.try_pop(item);
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
	tbb::concurrent_queue<T> q;
};
