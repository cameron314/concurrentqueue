// ©2013 Cameron Desrochers

// Provides the core enqueue/dequeue algorithm of moodycamel::ConcurrentQueue
// for testing with CDSChecker (a C++11 memory model checking tool).
// See http://demsky.eecs.uci.edu/c11modelchecker.html for more info.

#pragma once

#include "model-checker/include/atomic"
#include "model-checker/include/librace.h"
#include "model-checker/include/model-assert.h"

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

typedef unsigned int index_t;
static std::atomic<index_t> headIndex;
static std::atomic<index_t> tailIndex;
static std::atomic<index_t> dequeueOvercommit;
static std::atomic<index_t> dequeueOptimisticCount;

static const unsigned int BLOCK_SIZE = 256;
static int block[BLOCK_SIZE];

static void init()
{
	headIndex.store(0, std::memory_order_relaxed);
	tailIndex.store(0, std::memory_order_relaxed);
	dequeueOvercommit.store(0, std::memory_order_relaxed);
	dequeueOptimisticCount.store(0, std::memory_order_relaxed);
}

template<typename T>
static inline bool circular_less_than(T a, T b)
{
	return static_cast<T>(a - b) > static_cast<T>(static_cast<T>(1) << static_cast<T>(sizeof(T) * CHAR_BIT - 1));
}

static void enqueue(int element)
{
	index_t currentTailIndex = tailIndex.load(std::memory_order_relaxed);
	index_t newTailIndex = 1 + currentTailIndex;
	
	store_32(&block[currentTailIndex & (BLOCK_SIZE - 1)], element);
	
	tailIndex.store(newTailIndex, std::memory_order_release);
}

static bool try_dequeue(int& element)
{
	auto tail = tailIndex.load(std::memory_order_relaxed);
	auto overcommit = dequeueOvercommit.load(std::memory_order_relaxed);
	if (circular_less_than<index_t>(dequeueOptimisticCount.load(std::memory_order_relaxed) - overcommit, tail)) {
		// Might be something to dequeue, let's give it a try
		
		// Note that this if is purely for performance purposes in the common case when the queue is
		// empty and the values are eventually consistent -- we may enter here spuriously.
		
		// Note that whatever the values of overcommit and tail are, they are not going to change (unless we
		// change them) and must be the same value at this point (inside the if) as when the if condition was
		// evaluated.

		// We insert an acquire fence here to synchronize-with the release upon incrementing dequeueOvercommit below.
		// This ensures that whatever the value we got loaded into overcommit, the load of dequeueOptisticCount in
		// the fetch_add below will result in a value at least as recent as that (and therefore at least as large).
		// Note that I believe a compiler (signal) fence here would be sufficient due to the nature of fetch_add (all
		// read-modify-write operations are guaranteed to work on the latest value in the modification order), but
		// unfortunately that can't be shown to be correct using only the C++11 standard.
		// See http://stackoverflow.com/questions/18223161/what-are-the-c11-memory-ordering-guarantees-in-this-corner-case
		std::atomic_thread_fence(std::memory_order_acquire);
		
		// Increment optimistic counter, then check if it went over the boundary
		auto myDequeueCount = dequeueOptimisticCount.fetch_add(1, std::memory_order_relaxed);
		
		// Note that since dequeueOvercommit must be <= dequeueOptimisticCount (because dequeueOvercommit is only ever
		// incremented after dequeueOptimisticCount -- this is enforced in the `else` block below), and since we now
		// have a version of dequeueOptimisticCount that is at least as recent as overcommit (due to the release upon
		// incrementing dequeueOvercommit and the acquire above that synchronizes with it), overcommit <= myDequeueCount.
		MODEL_ASSERT(overcommit <= myDequeueCount);
		
		// Note that we reload tail here in case it changed; it will be the same value as before or greater, since
		// this load is sequenced after (happens after) the earlier load above. This is supported by read-read
		// coherance (as defined in the standard), explained here: http://en.cppreference.com/w/cpp/atomic/memory_order
		auto newTail = tailIndex.load(std::memory_order_acquire);
		MODEL_ASSERT(newTail >= tail);
		tail = newTail;
		if (circular_less_than<index_t>(myDequeueCount - overcommit, tail)) {
			// Guaranteed to be at least one element to dequeue!
			
			// Get the index. Note that since there's guaranteed to be at least one element, this
			// will never exceed the true value of tail (but may exceed the value we read above).
			auto index = headIndex.fetch_add(1, std::memory_order_acq_rel);
			
			// Dequeue
			element = load_32(&block[index & (BLOCK_SIZE - 1)]);
			
			return true;
		}
		else {
			// Wasn't anything to dequeue after all; make the effective dequeue count eventually consistent
			dequeueOvercommit.fetch_add(1, std::memory_order_release);		// Release so that the fetch_add on dequeueOptimisticCount is guaranteed to happen before this write
		}
	}

	return false;
}

static int size_approx()
{
	auto tail = tailIndex.load(std::memory_order_relaxed);
	auto head = headIndex.load(std::memory_order_relaxed);
	return circular_less_than(head, tail) ? static_cast<int>(tail - head) : 0;
}
