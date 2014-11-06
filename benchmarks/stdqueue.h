// ©2014 Cameron Desrochers.

#pragma once

#include <queue>

#include "wrappers.h"

// Simple wrapper around std::queue (not thread safe)
template<typename T>
class StdQueueWrapper
{
public:
	typedef DummyToken producer_token_t;
	typedef DummyToken consumer_token_t;
	
public:
	template<typename U>
	inline bool enqueue(U&& item)
	{
		q.push(std::forward<U>(item));
		return true;
	}
	
	inline bool try_dequeue(T& item)
	{
		if (q.empty()) {
			return false;
		}
		
		item = std::move(q.front());
		q.pop();
		return true;
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
	std::queue<T> q;
};
