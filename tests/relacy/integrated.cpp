// ©2015 Cameron Desrochers

// Tests various parts of the queue using the actual
// full implementation itself, instead of isolated
// components. This is much slower, but provides much
// better coverage too.

#define MCDBGQ_USE_RELACY
#include "../../concurrentqueue.h"

#include <string>

using namespace moodycamel;

struct SmallBlockTraits : public ConcurrentQueueDefaultTraits
{
	static const size_t BLOCK_SIZE = 2;
};


struct enqueue_explicit_one : rl::test_suite<enqueue_explicit_one, 2>
{
	ConcurrentQueue<int, SmallBlockTraits> q;
	
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
	ConcurrentQueue<int, SmallBlockTraits> q;
	
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


struct cycle_explicit : rl::test_suite<cycle_explicit, 3>
{
	ConcurrentQueue<int, SmallBlockTraits> q;
	
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
	
	//simulate<enqueue_explicit_one>(500000);
	//simulate<enqueue_explicit_many>(100000);
	simulate<cycle_explicit>(1000000);
	
	return 0;
}
