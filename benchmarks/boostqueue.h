#pragma once

#include <utility>

#include "boost/lockfree/queue.hpp"
#include "wrappers.h"

template<typename T>
struct BoostQueueWrapper
{
	public:
	typedef DummyToken producer_token_t;
	typedef DummyToken consumer_token_t;
	
public:
  BoostQueueWrapper() : q(/* starting capacity */ 16384) { }

	template<typename U>
	inline bool enqueue(U&& item)
	{
		return q.push(std::forward<U>(item));
	}
	
	inline bool try_dequeue(T& item)
	{
		return q.pop(item);
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
	boost::lockfree::queue<T> q;
};
