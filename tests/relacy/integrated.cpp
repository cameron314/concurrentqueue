// ©2015 Cameron Desrochers

// Tests various parts of the queue using the actual
// full implementation itself, instead of isolated
// components. This is much slower, but provides much
// better coverage too.

#define MCDBGQ_USE_RELACY
#include "../../concurrentqueue.h"

#include <string>

using namespace moodycamel;

struct SmallConstantTraits : public ConcurrentQueueDefaultTraits
{
	static const size_t BLOCK_SIZE = 2;
	static const size_t EXPLICIT_INITIAL_INDEX_SIZE = 2;
	static const size_t IMPLICIT_INITIAL_INDEX_SIZE = 2;
	static const size_t INITIAL_IMPLICIT_PRODUCER_HASH_SIZE = 1;
	static const std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = 4;
};


struct enqueue_explicit_one : rl::test_suite<enqueue_explicit_one, 2>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	
	void before()
	{
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		ProducerToken t(q);
		q.enqueue(t, tid);
		
		RelacyThreadExitNotifier::notify_relacy_thread_exit();
	}
	
	void after()
	{
		int tid0, tid1;
		RL_ASSERT(q.try_dequeue(tid0));
		RL_ASSERT(tid0 == 0 || tid0 == 1);
		RL_ASSERT(q.try_dequeue(tid1));
		RL_ASSERT(tid1 == 0 || tid1 == 1);
		RL_ASSERT(tid0 != tid1);
		RL_ASSERT(!q.try_dequeue(tid0));
	}
	
	void invariant()
	{
	}
};


struct enqueue_explicit_many : rl::test_suite<enqueue_explicit_many, 3>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	
	void before()
	{
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		ProducerToken t(q);
		for (int i = 0; i != 5; ++i) {
			q.enqueue(t, tid * 10 + i);
		}
		
		RelacyThreadExitNotifier::notify_relacy_thread_exit();
	}
	
	void after()
	{
		int item;
		for (int i = 0; i != 15; ++i) {
			RL_ASSERT(q.try_dequeue(item));
		}
		RL_ASSERT(!q.try_dequeue(item));
	}
	
	void invariant()
	{
	}
};


// This one caught a bug with the memory ordering in the core dequeue algorithm
struct dequeue_some_explicit : rl::test_suite<dequeue_some_explicit, 3>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	
	void before()
	{
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		if (tid <= 1) {
			int item;
			ConsumerToken t(q);
			for (int i = 0; i != 5; ++i) {
				q.try_dequeue(t, item);
			}
		}
		else {
			ProducerToken t(q);
			for (int i = 0; i != 3; ++i) {
				q.enqueue(t, tid * 10 + i);
			}
		}
		
		RelacyThreadExitNotifier::notify_relacy_thread_exit();
	}
	
	void after()
	{
	}
	
	void invariant()
	{
	}
};


// Causes blocks to be reused
struct recycle_blocks_explicit : rl::test_suite<recycle_blocks_explicit, 3>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	std::vector<bool> seen;
	
	void before()
	{
		seen.resize(8, false);
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		if (tid == 0) {
			ProducerToken t(q);
			for (int i = 0; i != 8; ++i) {
				q.enqueue(t, i);
			}
		}
		else {
			int item;
			ConsumerToken t(q);
			for (int i = 0; i != 6; ++i) {
				if (q.try_dequeue(t, item)) {
					RL_ASSERT(!seen[item]);
					seen[item] = true;
				}
			}
		}
		
		RelacyThreadExitNotifier::notify_relacy_thread_exit();
	}
	
	void after()
	{
		int item;
		while (q.try_dequeue(item)) {
			RL_ASSERT(!seen[item]);
			seen[item] = true;
		}
		for (auto s : seen) {
			RL_ASSERT(s);
		}
	}
	
	void invariant()
	{
	}
};

// Causes the explicit producer's block index to expand
struct expand_block_index_explicit : rl::test_suite<expand_block_index_explicit, 4>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	std::vector<bool> seen;
	
	void before()
	{
		seen.resize(12, false);
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		if (tid == 0) {
			ProducerToken t(q);
			for (int i = 0; i != 12; ++i) {
				q.enqueue(t, i);
			}
		}
		else {
			int item;
			ConsumerToken t(q);
			for (int i = 0; i != 3; ++i) {
				if (q.try_dequeue(t, item)) {
					RL_ASSERT(!seen[item]);
					seen[item] = true;
				}
			}
		}
		
		RelacyThreadExitNotifier::notify_relacy_thread_exit();
	}
	
	void after()
	{
		int item;
		while (q.try_dequeue(item)) {
			RL_ASSERT(!seen[item]);
			seen[item] = true;
		}
		for (auto s : seen) {
			RL_ASSERT(s);
		}
	}
	
	void invariant()
	{
	}
};


// Tests that implicit producers work at a very basic level
struct enqueue_implicit_one : rl::test_suite<enqueue_implicit_one, 2>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	
	void before()
	{
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		q.enqueue(tid);
		
		RelacyThreadExitNotifier::notify_relacy_thread_exit();
	}
	
	void after()
	{
		int tid0, tid1;
		RL_ASSERT(q.try_dequeue(tid0));
		RL_ASSERT(tid0 == 0 || tid0 == 1);
		RL_ASSERT(q.try_dequeue(tid1));
		RL_ASSERT(tid1 == 0 || tid1 == 1);
		RL_ASSERT(tid0 != tid1);
		RL_ASSERT(!q.try_dequeue(tid0));
	}
	
	void invariant()
	{
	}
};

// Tests implicit producer at a simple level
struct implicit_simple : rl::test_suite<implicit_simple, 3>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	std::vector<bool> seen;
	
	void before()
	{
		seen.resize(5, false);
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		if (tid == 0) {
			for (int i = 0; i != 5; ++i) {
				q.enqueue(i);
			}
		}
		else {
			int item;
			for (int i = 0; i != 3; ++i) {
				if (q.try_dequeue(item)) {
					RL_ASSERT(!seen[item]);
					seen[item] = true;
				}
			}
		}
		
		RelacyThreadExitNotifier::notify_relacy_thread_exit();
	}
	
	void after()
	{
		int item;
		while (q.try_dequeue(item)) {
			RL_ASSERT(!seen[item]);
			seen[item] = true;
		}
		for (auto s : seen) {
			RL_ASSERT(s);
		}
	}
	
	void invariant()
	{
	}
};


// Tests multiple implicit producers being created and/or reused

// Tests implicit producer block recycling

// Tests consumption from mixed producers


template<typename TTest>
void simulate(int iterations)
{
	rl::test_params randomParams;
	randomParams.search_type = rl::sched_random;
	randomParams.iteration_count = iterations;
	rl::simulate<TTest>(randomParams);
}

int main()
{
	// Note: There's no point using the full search params
	// Even with the simple enqueue_explicit_one test, it
	// would take a few millenia to complete(!)
	//rl::test_params fullParams;
	//fullParams.search_type = rl::sched_full;
	
	//simulate<enqueue_explicit_one>(1000000);
	//simulate<enqueue_explicit_many>(500000);
	//simulate<dequeue_some_explicit>(1000000);
	//simulate<recycle_blocks_explicit>(1000000);
	//simulate<expand_block_index_explicit>(1000000);
	//simulate<enqueue_implicit_one>(1000000);
	simulate<implicit_simple>(1000000);
	
	return 0;
}
