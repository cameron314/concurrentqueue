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
	static const std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = 2;
};

struct MediumConstantTraits : public ConcurrentQueueDefaultTraits
{
	static const size_t BLOCK_SIZE = 4;
	static const size_t EXPLICIT_INITIAL_INDEX_SIZE = 2;
	static const size_t IMPLICIT_INITIAL_INDEX_SIZE = 4;
	static const size_t INITIAL_IMPLICIT_PRODUCER_HASH_SIZE = 2;
	static const std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = 4;
};

struct Foo {
	static int& ctorCount() { static int c; return c; }
	static int& dtorCount() { static int c; return c; }
	static void reset() { ctorCount() = 0; dtorCount() = 0; }
	
	Foo()
		: id(-2)
	{
		++ctorCount();
	}
	
	Foo(int id)
		: id(id)
	{
		++ctorCount();
	}
	
	Foo(Foo const& o)
		: id(o.id)
	{
		++ctorCount();
	}
	
	~Foo()
	{
		RL_ASSERT(id != -1);
		++dtorCount();
		id = -1;
	}
	
public:
	int id;
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


// Tests multiple implicit producers being created (stresses the implicit producer hash map)
struct many_implicit_producers : rl::test_suite<many_implicit_producers, 6>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	std::vector<bool> seen;
	
	void before()
	{
		seen.resize(18, false);
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		q.enqueue(tid * 3 + 0);
		q.enqueue(tid * 3 + 1);
		q.enqueue(tid * 3 + 2);
		
		int item;
		for (int i = 0; i != 2; ++i) {
			if (q.try_dequeue(item)) {
				RL_ASSERT(!seen[item]);
				seen[item] = true;
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

// Tests multiple implicit producers being created (stresses the implicit producer hash map)
struct implicit_producer_reuse : rl::test_suite<implicit_producer_reuse, 9>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	std::vector<bool> seen;
	
	void before()
	{
		seen.resize(9, false);
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		q.enqueue(tid);
		
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

// Tests implicit producer block recycling
struct implicit_block_reuse : rl::test_suite<implicit_block_reuse, 4>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	std::vector<bool> seen;
	
	void before()
	{
		seen.resize(28, false);
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		for (int i = 0; i != 7; ++i) {
			q.enqueue(tid * 7 + i);
		}
		
		int item;
		ConsumerToken t(q);
		for (int i = 0; i != 7; ++i) {
			if (q.try_dequeue(t, item)) {
				RL_ASSERT(!seen[item]);
				seen[item] = true;
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

// Tests consumption from mixed producers
struct mixed : rl::test_suite<mixed, 4>
{
	ConcurrentQueue<int, SmallConstantTraits> q;
	std::vector<bool> seen;
	
	void before()
	{
		seen.resize(28, false);
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		if (tid <= 1) {
			for (int i = 0; i != 7; ++i) {
				q.enqueue(tid * 7 + i);
			}
		}
		else {
			ProducerToken t(q);
			for (int i = 0; i != 7; ++i) {
				q.enqueue(t, tid * 7 + i);
			}
		}
		
		int item;
		if (tid & 1) {
			for (int i = 0; i != 4; ++i) {
				if (q.try_dequeue(item)) {
					RL_ASSERT(!seen[item]);
					seen[item] = true;
				}
			}
		}
		else {
			ConsumerToken t(q);
			for (int i = 0; i != 4; ++i) {
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

// Test leftovers are being properly destroyed
struct leftovers_destroyed_explicit : rl::test_suite<leftovers_destroyed_explicit, 3>
{
	ConcurrentQueue<Foo, MediumConstantTraits>* q;
	std::vector<bool> seen;
	
	void before()
	{
		seen.resize(rl::rand(32), false);
		
		q = new ConcurrentQueue<Foo, MediumConstantTraits>();
		Foo::reset();
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		if (tid == 0) {
			ProducerToken t(*q);
			for (int i = 0; i != (int)seen.size(); ++i) {
				q->enqueue(t, Foo(i));
			}
		}
		else {
			Foo item;
			ConsumerToken t(*q);
			for (int i = rl::rand(17); i > 0; --i) {
				if (q->try_dequeue(t, item)) {
					RL_ASSERT(!seen[item.id]);
					seen[item.id] = true;
				}
			}
		}
		
		RelacyThreadExitNotifier::notify_relacy_thread_exit();
	}
	
	void after()
	{
		int seenCount = 0;
		{
			for (auto s : seen) {
				if (s) {
					++seenCount;
				}
			}
		}
		
		RL_ASSERT(Foo::ctorCount() == seen.size() * 2 + 2);
		RL_ASSERT(Foo::dtorCount() == seen.size() + seenCount + 2);
		delete q;
		
		RL_ASSERT(Foo::ctorCount() == seen.size() * 2 + 2);
		RL_ASSERT(Foo::ctorCount() == Foo::dtorCount());
	}
	
	void invariant()
	{
	}
};

// implicit
struct leftovers_destroyed_implicit : rl::test_suite<leftovers_destroyed_implicit, 3>
{
	ConcurrentQueue<Foo, MediumConstantTraits>* q;
	std::vector<bool> seen;
	
	void before()
	{
		seen.resize(rl::rand(32), false);
		
		q = new ConcurrentQueue<Foo, MediumConstantTraits>();
		Foo::reset();
	}
	
	void thread(unsigned int tid)
	{
		RelacyThreadExitNotifier::notify_relacy_thread_start();
		
		if (tid == 0) {
			for (int i = 0; i != (int)seen.size(); ++i) {
				q->enqueue(Foo(i));
			}
		}
		else {
			Foo item;
			for (int i = rl::rand(17); i > 0; --i) {
				if (q->try_dequeue(item)) {
					RL_ASSERT(!seen[item.id]);
					seen[item.id] = true;
				}
			}
		}
		
		RelacyThreadExitNotifier::notify_relacy_thread_exit();
	}
	
	void after()
	{
		int seenCount = 0;
		{
			for (auto s : seen) {
				if (s) {
					++seenCount;
				}
			}
		}
		
		RL_ASSERT(Foo::ctorCount() == seen.size() * 2 + 2);
		RL_ASSERT(Foo::dtorCount() == seen.size() + seenCount + 2);
		delete q;
		
		RL_ASSERT(Foo::ctorCount() == seen.size() * 2 + 2);
		RL_ASSERT(Foo::ctorCount() == Foo::dtorCount());
	}
	
	void invariant()
	{
	}
};


template<typename TTest>
void simulate(int iterations)
{
	// Note: There's no point using the full search params
	// Even with the simple enqueue_explicit_one test, it
	// would take a few millenia to complete(!)
	//rl::test_params fullParams;
	//fullParams.search_type = rl::sched_full;
	
	rl::test_params randomParams;
	randomParams.search_type = rl::sched_random;
	randomParams.iteration_count = iterations;
	rl::simulate<TTest>(randomParams);
}

int main()
{
	simulate<enqueue_explicit_one>(1000000);
	simulate<enqueue_explicit_many>(1000000);
	simulate<dequeue_some_explicit>(1000000);
	simulate<recycle_blocks_explicit>(1000000);
	simulate<expand_block_index_explicit>(1000000);
	simulate<enqueue_implicit_one>(1000000);
	simulate<implicit_simple>(1000000);
	simulate<many_implicit_producers>(500000);
	simulate<implicit_producer_reuse>(1000000);
	simulate<implicit_block_reuse>(1000000);
	simulate<mixed>(1000000);
	simulate<leftovers_destroyed_explicit>(1000000);
	simulate<leftovers_destroyed_implicit>(1000000);
	
	return 0;
}
