// ©2013-2014 Cameron Desrochers.
// Distributed under the simplified BSD license (see the LICENSE file that
// should have come with this file).

#pragma once

#include "wrappers.h"
#include <mutex>

// Naïve implementation of a simple lock-based queue. A std::mutex is obtained for every
// method. Note that while the queue size is not fixed, each enqueue operation allocates
// memory, and each dequeue frees memory.
template<typename T>
class LockBasedQueue
{
public:
	typedef DummyToken producer_token_t;
	typedef DummyToken consumer_token_t;
	
public:
	LockBasedQueue()
	{
		tail = nullptr;
		head = nullptr;
	}
	
	~LockBasedQueue()
	{
		while (head != nullptr) {
			Node* next = head->next;
			delete head;
			head = next;
		}
	}
	
	template<typename U>
	inline bool enqueue(U&& item)
	{
		Node* node = new Node(item);
		
		std::lock_guard<std::mutex> guard(mutex);
		if (tail == nullptr) {
			head = tail = node;
		}
		else {
			tail->next = node;
			tail = node;
		}
		
		return true;
	}
	
	inline bool try_dequeue(T& item)
	{
		std::lock_guard<std::mutex> guard(mutex);
		if (head == nullptr) {
			return false;
		}
		else {
			item = std::move(head->item);
			Node* next = head->next;
			delete head;
			head = next;
			if (head == nullptr) {
				tail = nullptr;
			}
			return true;
		}
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
	struct Node
	{
		T item;
		Node* next;
		
		template<typename U>
		Node(U&& item)
			: item(std::forward<U>(item)), next(nullptr)
		{
		}
	};
	
	std::mutex mutex;
	Node* head;
	Node* tail;
};
