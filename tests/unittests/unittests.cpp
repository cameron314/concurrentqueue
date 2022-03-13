// ©2013-2014 Cameron Desrochers.
// Distributed under the simplified BSD license (see the LICENSE file that
// should have come with this file).

// Unit tests for moodycamel::ConcurrentQueue

#define likely MAKE_SURE_LIKELY_MACRO_CAN_PEACEFULLY_COEXIST
#define unlikely MAKE_SURE_UNLIKELY_MACRO_CAN_PEACEFULLY_COEXIST

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstddef>
#include <string>
#include <iterator>

struct MakeSureCustomNewCanPeacefullyCoexist;
void* operator new(size_t size, MakeSureCustomNewCanPeacefullyCoexist* x);
void operator delete(void* ptr, MakeSureCustomNewCanPeacefullyCoexist* x);

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>		// Not because we need it, but to ensure no conflicts arise with the queue's declarations
#endif

#include "minitest.h"
#include "../common/simplethread.h"
#include "../common/systemtime.h"
#include "../../concurrentqueue.h"
#include "../../blockingconcurrentqueue.h"
#include "../../c_api/concurrentqueue.h"

namespace {
	struct tracking_allocator
	{
		union tag {
			std::size_t size;
#ifdef __GNUC__
			max_align_t dummy;		// GCC forgot to add it to std:: for a while
#else
			std::max_align_t dummy;	// Others (e.g. MSVC) insist it can *only* be accessed via std::
#endif
		};
		
		static inline void* malloc(std::size_t size)
		{
			auto ptr = std::malloc(size + sizeof(tag));
			if (ptr) {
				reinterpret_cast<tag*>(ptr)->size = size;
				usage.fetch_add(size, std::memory_order_relaxed);
				return reinterpret_cast<char*>(ptr) + sizeof(tag);
			}
			return nullptr;
		}
		
		static inline void free(void* ptr)
		{
			if (ptr) {
				ptr = reinterpret_cast<char*>(ptr) - sizeof(tag);
				auto size = reinterpret_cast<tag*>(ptr)->size;
				usage.fetch_add(-size, std::memory_order_relaxed);
			}
			std::free(ptr);
		}
		
		static inline std::size_t current_usage() { return usage.load(std::memory_order_relaxed); }
		
	private:
		static std::atomic<std::size_t> usage;
	};
	
	std::atomic<std::size_t> tracking_allocator::usage(0);
}

struct corealgos_allocator
{
	static inline void* malloc(std::size_t size) { return tracking_allocator::malloc(size); }
	static inline void free(void* ptr) { tracking_allocator::free(ptr); }
};

#define corealgos_allocator corealgos_allocator

#include "../corealgos.h"

using namespace moodycamel;


namespace moodycamel
{
struct MallocTrackingTraits : public ConcurrentQueueDefaultTraits
{
	static inline void* malloc(std::size_t size) { return tracking_allocator::malloc(size); }
	static inline void free(void* ptr) { tracking_allocator::free(ptr); }
};

template<std::size_t BlockSize = ConcurrentQueueDefaultTraits::BLOCK_SIZE, std::size_t InitialIndexSize = ConcurrentQueueDefaultTraits::EXPLICIT_INITIAL_INDEX_SIZE, bool RecycleBlocks = ConcurrentQueueDefaultTraits::RECYCLE_ALLOCATED_BLOCKS>
struct TestTraits : public MallocTrackingTraits
{
	typedef std::size_t size_t;
	typedef uint64_t index_t;
	
	static const size_t BLOCK_SIZE = BlockSize;
	static const size_t EXPLICIT_INITIAL_INDEX_SIZE = InitialIndexSize;
	static const size_t IMPLICIT_INITIAL_INDEX_SIZE = InitialIndexSize * 2;
	static const bool RECYCLE_ALLOCATED_BLOCKS = RecycleBlocks;
	
	static inline void reset() { _malloc_count() = 0; _free_count() = 0; }
	static inline std::atomic<int>& _malloc_count() { static std::atomic<int> c; return c; }
	static inline int malloc_count() { return _malloc_count().load(std::memory_order_seq_cst); }
	static inline std::atomic<int>& _free_count() { static std::atomic<int> c; return c; }
	static inline int free_count() { return _free_count().load(std::memory_order_seq_cst); }
	
	static inline void* malloc(ConcurrentQueueDefaultTraits::size_t bytes) { ++_malloc_count(); return tracking_allocator::malloc(bytes); }
	static inline void free(void* obj) { ++_free_count(); return tracking_allocator::free(obj); }
};

struct SmallIndexTraits : public MallocTrackingTraits
{
	typedef uint16_t size_t;
	typedef uint16_t index_t;
};

struct ExtraSmallIndexTraits : public MallocTrackingTraits
{
	typedef uint8_t size_t;
	typedef uint8_t index_t;
};

struct LargeTraits : public MallocTrackingTraits
{
	static const size_t BLOCK_SIZE = 128;
	static const size_t INITIAL_IMPLICIT_PRODUCER_HASH_SIZE = 128;
	static const size_t IMPLICIT_INITIAL_INDEX_SIZE = 128;
};

// Note: Not thread safe!
struct Foo
{
	static int& nextId() { static int i; return i; }
	static int& createCount() { static int c; return c; }
	static int& destroyCount() { static int c; return c; }
	static bool& destroyedInOrder() { static bool d = true; return d; }
	static void reset() { createCount() = 0; destroyCount() = 0; nextId() = 0; destroyedInOrder() = true; lastDestroyedId() = -1; }
	
	Foo() { id = nextId()++; ++createCount(); }
	Foo(Foo const&) MOODYCAMEL_DELETE_FUNCTION;
	Foo(Foo&& other) { id = other.id; other.id = -1; }
	void operator=(Foo&& other) { id = other.id; other.id = -1; }
	~Foo()
	{
		++destroyCount();
		if (id == -2) {
			// Double free!
			destroyedInOrder() = false;
		}
		else if (id != -1) {
			if (id <= lastDestroyedId()) {
				destroyedInOrder() = false;
			}
			lastDestroyedId() = id;
		}
		id = -2;
	}
	
private:
	int id;
	static int& lastDestroyedId() { static int i = -1; return i; }
};

struct Copyable {
	Copyable(int id) : copied(false), id(id) { }
	Copyable(Copyable const& o) : copied(true), id(o.id) { }
	void operator=(Copyable const& o) { copied = true; id = o.id; }
	bool copied;
	int id;
};

struct Moveable {
	Moveable(int id) : moved(false), copied(false), id(id) { }
	Moveable(Moveable&& o) MOODYCAMEL_NOEXCEPT : moved(true), copied(o.copied), id(o.id) { }
	void operator=(Moveable&& o) MOODYCAMEL_NOEXCEPT { moved = true; copied = o.copied; id = o.id; }
	bool moved;
	bool copied;
	int id;

#if defined(_MSC_VER) && _MSC_VER < 1800
	// VS2012's std::is_nothrow_[move_]constructible is broken, so the queue never attempts to
	// move objects with that compiler. In this case, we don't know whether it's really a copy
	// or not being done, so give the benefit of the doubt (given the tests pass on other platforms)
	// and assume it would have done a move if it could have (don't set copied to true).
	Moveable(Moveable const& o) MOODYCAMEL_NOEXCEPT : moved(o.moved), copied(o.copied), id(o.id) { }
	void operator=(Moveable const& o) MOODYCAMEL_NOEXCEPT { moved = o.moved; copied = o.copied; id = o.id; }
#else
	Moveable(Moveable const& o) MOODYCAMEL_NOEXCEPT : moved(o.moved), copied(true), id(o.id) { }
	void operator=(Moveable const& o) MOODYCAMEL_NOEXCEPT { moved = o.moved; copied = true; id = o.id; }
#endif
};

struct ThrowingMovable {
	static std::atomic<int>& ctorCount() { static std::atomic<int> c; return c; }
	static std::atomic<int>& destroyCount() { static std::atomic<int> c; return c; }
	static void reset() { ctorCount() = 0; destroyCount() = 0; }
	
	explicit ThrowingMovable(int id, bool throwOnCctor = false, bool throwOnAssignment = false, bool throwOnSecondCctor = false)
		: id(id), moved(false), copied(false), throwOnCctor(throwOnCctor), throwOnAssignment(throwOnAssignment), throwOnSecondCctor(throwOnSecondCctor)
	{
		ctorCount().fetch_add(1, std::memory_order_relaxed);
	}
	
	ThrowingMovable(ThrowingMovable const& o)
		: id(o.id), moved(false), copied(true), throwOnCctor(o.throwOnCctor), throwOnAssignment(o.throwOnAssignment), throwOnSecondCctor(false)
	{
		if (throwOnCctor) {
			throw this;
		}
		ctorCount().fetch_add(1, std::memory_order_relaxed);
		throwOnCctor = o.throwOnSecondCctor;
	}
	
	ThrowingMovable(ThrowingMovable&& o)
		: id(o.id), moved(true), copied(false), throwOnCctor(o.throwOnCctor), throwOnAssignment(o.throwOnAssignment), throwOnSecondCctor(false)
	{
		if (throwOnCctor) {
			throw this;
		}
		ctorCount().fetch_add(1, std::memory_order_relaxed);
		throwOnCctor = o.throwOnSecondCctor;
	}
	
	~ThrowingMovable()
	{
		destroyCount().fetch_add(1, std::memory_order_relaxed);
	}
	
	void operator=(ThrowingMovable const& o)
	{
		id = o.id;
		moved = false;
		copied = true;
		throwOnCctor = o.throwOnCctor;
		throwOnAssignment = o.throwOnAssignment;
		throwOnSecondCctor = o.throwOnSecondCctor;
		if (throwOnAssignment) {
			throw this;
		}
	}
	
	void operator=(ThrowingMovable&& o)
	{
		id = o.id;
		moved = true;
		copied = false;
		throwOnCctor = o.throwOnCctor;
		throwOnAssignment = o.throwOnAssignment;
		throwOnSecondCctor = o.throwOnSecondCctor;
		if (throwOnAssignment) {
			throw this;
		}
	}
	
	int id;
	bool moved;
	bool copied;
	
public:
	bool throwOnCctor;
	bool throwOnAssignment;
	bool throwOnSecondCctor;
};

#ifdef __arm__
#define SUPER_ALIGNMENT 64
#else
#define SUPER_ALIGNMENT 128
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)  // structure was padded due to alignment specifier
#endif

struct MOODYCAMEL_ALIGNAS(SUPER_ALIGNMENT) VeryAligned {
	static size_t errors;

	int value;

	VeryAligned() MOODYCAMEL_NOEXCEPT : value(0) {
		if (reinterpret_cast<uintptr_t>(this) % SUPER_ALIGNMENT != 0)
			++errors;
	}

	VeryAligned(int value) MOODYCAMEL_NOEXCEPT : value(value) {
		if (reinterpret_cast<uintptr_t>(this) % SUPER_ALIGNMENT != 0)
			++errors;
	}

	VeryAligned(VeryAligned&& x) MOODYCAMEL_NOEXCEPT : value(x.value) {
		if (reinterpret_cast<uintptr_t>(this) % SUPER_ALIGNMENT != 0)
			++errors;
		x.value = 0;
	}

	VeryAligned& operator=(VeryAligned&& x) MOODYCAMEL_NOEXCEPT {
		std::swap(value, x.value);
		return *this;
	}

	VeryAligned(VeryAligned const&) MOODYCAMEL_DELETE_FUNCTION;
	VeryAligned& operator=(VeryAligned const&) MOODYCAMEL_DELETE_FUNCTION;
};
size_t VeryAligned::errors = 0;

#ifdef _MSC_VER
#pragma warning(pop)
#endif



class ConcurrentQueueTests : public TestClass<ConcurrentQueueTests>
{
public:
	ConcurrentQueueTests()
	{
		REGISTER_TEST(create_empty_queue);
		REGISTER_TEST(create_token);
		REGISTER_TEST(circular_less_than);
		REGISTER_TEST(enqueue_one_explicit);
		REGISTER_TEST(enqueue_and_dequeue_one_explicit);
		REGISTER_TEST(enqueue_one_implicit);
		REGISTER_TEST(enqueue_and_dequeue_one_implicit);
		REGISTER_TEST(enqueue_and_dequeue_a_few);
		REGISTER_TEST(enqueue_bulk);
		REGISTER_TEST(block_alloc);
		REGISTER_TEST(token_move);
		REGISTER_TEST(multi_producers);
		REGISTER_TEST(producer_reuse);
		REGISTER_TEST(block_reuse);
		REGISTER_TEST(block_recycling);
		REGISTER_TEST(leftovers_destroyed);
		REGISTER_TEST(block_index_resized);
		REGISTER_TEST(try_dequeue);
		REGISTER_TEST(try_dequeue_threaded);
		REGISTER_TEST(try_dequeue_bulk);
		REGISTER_TEST(try_dequeue_bulk_threaded);
		REGISTER_TEST(implicit_producer_hash);
		REGISTER_TEST(index_wrapping);
		REGISTER_TEST(subqueue_size_limit);
		REGISTER_TEST(exceptions);
		REGISTER_TEST(implicit_producer_churn);
		REGISTER_TEST(test_threaded);
		REGISTER_TEST(test_threaded_bulk);
		REGISTER_TEST(full_api<ConcurrentQueueDefaultTraits>);
		REGISTER_TEST(full_api<SmallIndexTraits>);
		REGISTER_TEST(blocking_wrappers);
		REGISTER_TEST(timed_blocking_wrappers);

		//c_api/concurrentqueue
		REGISTER_TEST(c_api_create);
		REGISTER_TEST(c_api_enqueue);
		REGISTER_TEST(c_api_try_dequeue);
		REGISTER_TEST(c_api_destroy);
		
		// Semaphore
		REGISTER_TEST(acquire_and_signal);
		REGISTER_TEST(try_acquire_and_signal);
		
		// Core algos
		REGISTER_TEST(core_add_only_list);
		REGISTER_TEST(core_thread_local);
		REGISTER_TEST(core_free_list);
		REGISTER_TEST(core_spmc_hash);
		
		REGISTER_TEST(explicit_strings_threaded);
		REGISTER_TEST(large_traits);
	}
	
	bool postTest(bool testSucceeded) override
	{
		if (testSucceeded) {
			// If this assertion fails, there's necessarily a memory leak somewhere!
			ASSERT_OR_FAIL(tracking_allocator::current_usage() == 0);
		}
		return true;
	}
	
	
	bool create_empty_queue()
	{
		ConcurrentQueue<int, MallocTrackingTraits> q;
		return true;
	}
	
	
	bool create_token()
	{
		ConcurrentQueue<int, MallocTrackingTraits> q;
		ProducerToken tok(q);
		
		return true;
	}
	
	bool circular_less_than()
	{
		{
			uint32_t a, b;
			
			a = 0; b = 100;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 100; b = 0;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(details::circular_less_than(b, a));
			
			a = 0; b = 0;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 100; b = 100;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 0; b = 1u << 31;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 1; b = 1u << 31;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 0; b = (1u << 31) + 1;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(details::circular_less_than(b, a));
			
			a = 100; b = (1u << 31) + 1;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = (1u << 31) + 7; b = 5;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = (1u << 16) + 7; b = (1 << 16) + 5;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(details::circular_less_than(b, a));
			
			a = 0xFFFFFFFFu; b = 0;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 0xFFFFFFFFu; b = 0xFFFFFFu;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
		}
		
		{
			uint16_t a, b;
			
			a = 0; b = 100;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 100; b = 0;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(details::circular_less_than(b, a));
			
			a = 0; b = 0;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 100; b = 100;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 0; b = 1 << 15;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 1; b = 1 << 15;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 0; b = (1 << 15) + 1;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(details::circular_less_than(b, a));
			
			a = 100; b = (1 << 15) + 1;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = (1 << 15) + 7; b = 5;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = (1 << 15) + 7; b = (1 << 15) + 5;
			ASSERT_OR_FAIL(!details::circular_less_than(a, b));
			ASSERT_OR_FAIL(details::circular_less_than(b, a));
			
			a = 0xFFFF; b = 0;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
			
			a = 0xFFFF; b = 0xFFF;
			ASSERT_OR_FAIL(details::circular_less_than(a, b));
			ASSERT_OR_FAIL(!details::circular_less_than(b, a));
		}
		
		return true;
	}
	
	
	bool enqueue_one_explicit()
	{
		ConcurrentQueue<int, MallocTrackingTraits> q;
		ProducerToken tok(q);
		
		bool result = q.enqueue(tok, 17);
		
		ASSERT_OR_FAIL(result);
		return true;
	}
	
	bool enqueue_and_dequeue_one_explicit()
	{
		ConcurrentQueue<int, MallocTrackingTraits> q;
		ProducerToken tok(q);
		
		int item = 0;
		ASSERT_OR_FAIL(q.enqueue(tok, 123));
		ASSERT_OR_FAIL(q.try_dequeue_from_producer(tok, item));
		ASSERT_OR_FAIL(item == 123);
		
		return true;
	}
	
	bool enqueue_one_implicit()
	{
		ConcurrentQueue<int, MallocTrackingTraits> q;
		
		bool result = q.enqueue(17);
		
		ASSERT_OR_FAIL(result);
		return true;
	}
	
	bool enqueue_and_dequeue_one_implicit()
	{
		ConcurrentQueue<int, MallocTrackingTraits> q;
		
		int item = 0;
		ASSERT_OR_FAIL(q.enqueue(123));
		ASSERT_OR_FAIL(q.try_dequeue(item));
		ASSERT_OR_FAIL(item == 123);
		
		return true;
	}
	
	bool enqueue_and_dequeue_a_few()
	{
		// Fairly straightforward mass enqueue and dequeue
		{
			ConcurrentQueue<int, TestTraits<16>> q;
			ProducerToken tok(q);
			
			for (int i = 0; i != 99999; ++i) {
				ASSERT_OR_FAIL(q.enqueue(tok, i));
			}
			
			int item;
			for (int i = 0; i != 99999; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue_from_producer(tok, item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q.try_dequeue_from_producer(tok, item));
		}
		
		// Interleaved enqueue and dequeue (though still no threads involved)
		{
			ConcurrentQueue<int, TestTraits<16>> q;
			ProducerToken tok(q);
			
			int item;
			for (int i = 0; i != 99999; ++i) {
				ASSERT_OR_FAIL(q.enqueue(tok, i));
				ASSERT_OR_FAIL(q.enqueue(tok, i * 2));
				ASSERT_OR_FAIL(q.try_dequeue_from_producer(tok, item));
				ASSERT_OR_FAIL(item == (i / 2) * (i % 2 == 0 ? 1 : 2));
			}
			
			for (int i = 0; i != 99999; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue_from_producer(tok, item));
				ASSERT_OR_FAIL(item == ((i + 99999) / 2) * (i % 2 == 1 ? 1 : 2));
			}
			ASSERT_OR_FAIL(!q.try_dequeue_from_producer(tok, item));
		}
		
		// Implicit usage
		{
			ConcurrentQueue<int, TestTraits<16>> q;
			
			for (int i = 0; i != 99999; ++i) {
				ASSERT_OR_FAIL(q.enqueue(i));
			}
			
			int item;
			for (int i = 0; i != 99999; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		{
			ConcurrentQueue<int, TestTraits<16>> q;
			
			int item;
			for (int i = 0; i != 99999; ++i) {
				ASSERT_OR_FAIL(q.enqueue(i));
				ASSERT_OR_FAIL(q.enqueue(i * 2));
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == (i / 2) * (i % 2 == 0 ? 1 : 2));
			}
			
			for (int i = 0; i != 99999; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == ((i + 99999) / 2) * (i % 2 == 1 ? 1 : 2));
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		return true;
	}
	
	bool enqueue_bulk()
	{
		typedef TestTraits<2> Traits2;
		typedef TestTraits<4> Traits4;
		
		int arr123[] = { 1, 2, 3 };
		int arr1234[] = { 1, 2, 3, 4 };
		int arr123456[] = { 1, 2, 3, 4, 5, 6 };
		
		Traits2::reset();
		{
			// Implicit, block allocation required
			ConcurrentQueue<int, Traits2> q(2);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 1);
			
			q.enqueue_bulk(arr123, 3);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 4);		// One for producer, one for block index, one for block
			
			int item;
			for (int i = 0; i != 3; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i + 1);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits4::reset();
		{
			// Implicit, block allocation not required (end on block boundary)
			ConcurrentQueue<int, Traits4> q(2);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 1);
			
			q.enqueue_bulk(arr1234, 4);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);		// One for producer, one for block index
			
			int item;
			for (int i = 0; i != 4; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i + 1);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits2::reset();
		{
			// Implicit, allocation fail
			ConcurrentQueue<int, Traits2> q(2);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 1);
			
			ASSERT_OR_FAIL(!q.try_enqueue_bulk(arr123, 3));
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);		// Still has to allocate implicit producer and block index
			
			int item;
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			
			ASSERT_OR_FAIL(q.try_enqueue_bulk(arr123, 2));
			for (int i = 0; i != 2; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i + 1);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			
		}
		
		Traits2::reset();
		{
			// Implicit, block allocation not required
			ConcurrentQueue<int, Traits2> q(4);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 1);
			
			q.enqueue_bulk(arr1234, 4);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);		// One for producer, one for block index
			
			int item;
			for (int i = 0; i != 4; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i + 1);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits4::reset();
		{
			// Implicit, block allocation required (end not on block boundary)
			ConcurrentQueue<int, Traits4> q(4);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 1);
			
			ASSERT_OR_FAIL(q.enqueue(0));
			
			ASSERT_OR_FAIL(q.enqueue_bulk(arr1234, 4));
			ASSERT_OR_FAIL(Traits4::malloc_count() == 4);		// One for producer, one for block index, one for block
			
			int item;
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits4::reset();
		{
			// Implicit, block allocation not required (end not on block boundary)
			ConcurrentQueue<int, Traits4> q(5);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 1);
			
			ASSERT_OR_FAIL(q.enqueue(0));
			
			ASSERT_OR_FAIL(q.enqueue_bulk(arr1234, 4));
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);		// One for producer, one for block index
			
			int item;
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits2::reset();
		{
			// Implicit, block allocation fail (end not on block boundary) -- test rewind
			ConcurrentQueue<int, Traits2> q(4);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 1);
			
			ASSERT_OR_FAIL(q.enqueue(17));
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);		// One for producer, one for block index
			
			ASSERT_OR_FAIL(!q.try_enqueue_bulk(arr123456, 6));
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);
			
			int item;
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item == 17);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits2::reset();
		{
			// Implicit, enqueue nothing
			ConcurrentQueue<int, Traits2> q(3);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 1);
			
			ASSERT_OR_FAIL(q.try_enqueue_bulk(arr123, 0));
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);		// One for producer, one for block index
			
			int item;
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		////////
		
		Traits2::reset();
		{
			// Explicit, block allocation required
			ConcurrentQueue<int, Traits2> q(2);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 1);
			
			ProducerToken tok(q);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);		// One for producer, one for block index
			
			q.enqueue_bulk(tok, arr123, 3);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 4);		// One for block
			
			int item;
			for (int i = 0; i != 3; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i + 1);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits4::reset();
		{
			// Explicit, block allocation not required (end on block boundary)
			ConcurrentQueue<int, Traits4> q(2);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 1);
			
			ProducerToken tok(q);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);		// One for producer, one for block index
			
			q.enqueue_bulk(tok, arr1234, 4);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);
			
			int item;
			for (int i = 0; i != 4; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i + 1);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits2::reset();
		{
			// Explicit, allocation fail
			ConcurrentQueue<int, Traits2> q(2);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 1);
			
			ProducerToken tok(q);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);		// One for producer, one for block index
			
			ASSERT_OR_FAIL(!q.try_enqueue_bulk(tok, arr123, 3));
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);
			
			int item;
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			
			ASSERT_OR_FAIL(q.try_enqueue_bulk(tok, arr123, 2));
			for (int i = 0; i != 2; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i + 1);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);
		}
		
		Traits2::reset();
		{
			// Explicit, block allocation not required
			ConcurrentQueue<int, Traits2> q(4);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 1);
			
			ProducerToken tok(q);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);		// One for producer, one for block index
			
			q.enqueue_bulk(tok, arr1234, 4);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);
			
			int item;
			for (int i = 0; i != 4; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i + 1);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits4::reset();
		{
			// Explicit, block allocation required (end not on block boundary)
			ConcurrentQueue<int, Traits4> q(4);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 1);
			
			ProducerToken tok(q);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);		// One for producer, one for block index
			
			ASSERT_OR_FAIL(q.enqueue(tok, 0));
			
			ASSERT_OR_FAIL(q.enqueue_bulk(tok, arr1234, 4));
			ASSERT_OR_FAIL(Traits4::malloc_count() == 4);		// One for block
			
			int item;
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits4::reset();
		{
			// Explicit, block allocation not required (end not on block boundary)
			ConcurrentQueue<int, Traits4> q(5);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 1);
			
			ProducerToken tok(q);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);		// One for producer, one for block index
			
			ASSERT_OR_FAIL(q.enqueue(tok, 0));
			
			ASSERT_OR_FAIL(q.enqueue_bulk(tok, arr1234, 4));
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);
			
			int item;
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits2::reset();
		{
			// Explicit, block allocation fail (end not on block boundary) -- test rewind
			ConcurrentQueue<int, Traits2> q(4);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 1);
			
			ProducerToken tok(q);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);		// One for producer, one for block index
			
			ASSERT_OR_FAIL(q.enqueue(tok, 17));
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);
			
			ASSERT_OR_FAIL(!q.try_enqueue_bulk(tok, arr123456, 6));
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);
			
			int item;
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item == 17);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits2::reset();
		{
			// Explicit, enqueue nothing
			ConcurrentQueue<int, Traits2> q(3);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 1);
			
			ProducerToken tok(q);
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);		// One for producer, one for block index
			
			ASSERT_OR_FAIL(q.try_enqueue_bulk(tok, arr123, 0));
			ASSERT_OR_FAIL(Traits2::malloc_count() == 3);
			
			int item;
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			
			ASSERT_OR_FAIL(q.enqueue(tok, 17));
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item == 17);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		Traits4::reset();
		{
			// Explicit, re-use empty blocks
			ConcurrentQueue<int, Traits4> q(8);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 1);
			
			ProducerToken tok(q);
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);		// One for producer, one for block index
			
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(q.enqueue(tok, i));
			}
			int item;
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);
			
			ASSERT_OR_FAIL(q.enqueue_bulk(tok, arr123456, 6));
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);
			
			for (int i = 0; i != 6; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i + 1);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			ASSERT_OR_FAIL(Traits4::malloc_count() == 3);
		}
		
		return true;
	}
	
	bool block_alloc()
	{
		typedef TestTraits<2> Traits;
		typedef TestTraits<2, 32, true> RecycleTraits;
		Traits::reset();
		
		// Explicit
		{
			ConcurrentQueue<int, Traits> q(7);
			ASSERT_OR_FAIL(q.initialBlockPoolSize == 4);
			
			ASSERT_OR_FAIL(Traits::malloc_count() == 1);
			ASSERT_OR_FAIL(Traits::free_count() == 0);
			
			{
				ProducerToken tok(q);
				ASSERT_OR_FAIL(Traits::malloc_count() == 3);		// one for producer, one for its block index
				ASSERT_OR_FAIL(Traits::free_count() == 0);
				
				// Enqueue one item too many (force extra block allocation)
				for (int i = 0; i != 9; ++i) {
					ASSERT_OR_FAIL(q.enqueue(tok, i));
				}
				
				ASSERT_OR_FAIL(Traits::malloc_count() == 4);
				ASSERT_OR_FAIL(Traits::free_count() == 0);
				
				// Still room for one more...
				ASSERT_OR_FAIL(q.enqueue(tok, 9));
				ASSERT_OR_FAIL(Traits::malloc_count() == 4);
				ASSERT_OR_FAIL(Traits::free_count() == 0);
				
				// No more room without further allocations
				ASSERT_OR_FAIL(!q.try_enqueue(tok, 10));
				ASSERT_OR_FAIL(Traits::malloc_count() == 4);
				ASSERT_OR_FAIL(Traits::free_count() == 0);
				
				// Check items were enqueued properly
				int item;
				for (int i = 0; i != 10; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue_from_producer(tok, item));
					ASSERT_OR_FAIL(item == i);
				}
				
				// Queue should be empty, but not freed
				ASSERT_OR_FAIL(!q.try_dequeue_from_producer(tok, item));
				ASSERT_OR_FAIL(Traits::free_count() == 0);
			}
			// Explicit producers are recycled, so block should still be allocated
			ASSERT_OR_FAIL(Traits::free_count() == 0);
		}
		
		ASSERT_OR_FAIL(Traits::malloc_count() == 4);
		ASSERT_OR_FAIL(Traits::free_count() == 4);
		
		// Implicit
		Traits::reset();
		{
			ConcurrentQueue<int, Traits> q(7);
			ASSERT_OR_FAIL(q.initialBlockPoolSize == 4);
			
			ASSERT_OR_FAIL(q.enqueue(39));
			
			ASSERT_OR_FAIL(Traits::malloc_count() == 3);		// one for producer, one for its block index
			ASSERT_OR_FAIL(Traits::free_count() == 0);
			
			// Enqueue one item too many (force extra block allocation)
			for (int i = 0; i != 8; ++i) {
				ASSERT_OR_FAIL(q.enqueue(i));
			}
			
			ASSERT_OR_FAIL(Traits::malloc_count() == 4);
			ASSERT_OR_FAIL(Traits::free_count() == 0);
			
			// Still room for one more...
			ASSERT_OR_FAIL(q.enqueue(8));
			ASSERT_OR_FAIL(Traits::malloc_count() == 4);
			ASSERT_OR_FAIL(Traits::free_count() == 0);
			
			// No more room without further allocations
			ASSERT_OR_FAIL(!q.try_enqueue(9));
			ASSERT_OR_FAIL(Traits::malloc_count() == 4);
			ASSERT_OR_FAIL(Traits::free_count() == 0);
			
			// Check items were enqueued properly
			int item;
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item == 39);
			for (int i = 0; i != 9; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i);
			}
			
			// Queue should be empty, and extra block freed
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			ASSERT_OR_FAIL(Traits::free_count() == 1);
		}
		
		ASSERT_OR_FAIL(Traits::malloc_count() == 4);
		ASSERT_OR_FAIL(Traits::free_count() == 4);
		
		// Implicit
		RecycleTraits::reset();
		{
			ConcurrentQueue<int, RecycleTraits> q(7);
			ASSERT_OR_FAIL(q.initialBlockPoolSize == 4);
			
			ASSERT_OR_FAIL(q.enqueue(39));
			
			ASSERT_OR_FAIL(RecycleTraits::malloc_count() == 3);		// one for producer, one for its block index
			ASSERT_OR_FAIL(RecycleTraits::free_count() == 0);
			
			// Enqueue one item too many (force extra block allocation)
			for (int i = 0; i != 8; ++i) {
				ASSERT_OR_FAIL(q.enqueue(i));
			}
			
			ASSERT_OR_FAIL(RecycleTraits::malloc_count() == 4);
			ASSERT_OR_FAIL(RecycleTraits::free_count() == 0);
			
			// Still room for one more...
			ASSERT_OR_FAIL(q.enqueue(8));
			ASSERT_OR_FAIL(RecycleTraits::malloc_count() == 4);
			ASSERT_OR_FAIL(RecycleTraits::free_count() == 0);
			
			// No more room without further allocations
			ASSERT_OR_FAIL(!q.try_enqueue(9));
			ASSERT_OR_FAIL(RecycleTraits::malloc_count() == 4);
			ASSERT_OR_FAIL(RecycleTraits::free_count() == 0);
			
			// Check items were enqueued properly
			int item;
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item == 39);
			for (int i = 0; i != 9; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i);
			}
			
			// Queue should be empty, but extra block not freed
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			ASSERT_OR_FAIL(RecycleTraits::free_count() == 0);
		}
		ASSERT_OR_FAIL(RecycleTraits::malloc_count() == 4);
		ASSERT_OR_FAIL(RecycleTraits::free_count() == 4);

		// Super-aligned
		Traits::reset();
		VeryAligned::errors = 0;
		{
			ConcurrentQueue<VeryAligned, Traits> q(7);
			ASSERT_OR_FAIL(q.enqueue(39));

			ASSERT_OR_FAIL(Traits::malloc_count() == 3);		// one for producer, one for its block index
			ASSERT_OR_FAIL(Traits::free_count() == 0);
			ASSERT_OR_FAIL(VeryAligned::errors == 0);

			// Enqueue one item too many (force extra block allocation)
			for (int i = 0; i != 8; ++i) {
				ASSERT_OR_FAIL(q.enqueue(i));
				ASSERT_OR_FAIL(VeryAligned::errors == 0);
			}

			ASSERT_OR_FAIL(Traits::malloc_count() == 4);
			ASSERT_OR_FAIL(Traits::free_count() == 0);

			// Still room for one more...
			ASSERT_OR_FAIL(q.enqueue(8));
			ASSERT_OR_FAIL(Traits::malloc_count() == 4);
			ASSERT_OR_FAIL(Traits::free_count() == 0);
			ASSERT_OR_FAIL(VeryAligned::errors == 0);

			// No more room without further allocations
			ASSERT_OR_FAIL(!q.try_enqueue(9));
			ASSERT_OR_FAIL(Traits::malloc_count() == 4);
			ASSERT_OR_FAIL(Traits::free_count() == 0);
			ASSERT_OR_FAIL(VeryAligned::errors == 0);

			// Check items were enqueued properly
			VeryAligned item;
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.value == 39);
			for (int i = 0; i != 9; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item.value == i);
				ASSERT_OR_FAIL(VeryAligned::errors == 0);
			}

			// Queue should be empty, and extra block freed
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			ASSERT_OR_FAIL(Traits::free_count() == 1);
			ASSERT_OR_FAIL(VeryAligned::errors == 0);
		}

		ASSERT_OR_FAIL(Traits::malloc_count() == 4);
		ASSERT_OR_FAIL(Traits::free_count() == 4);

		return true;
	}
	
	bool token_move()
	{
		typedef TestTraits<16> Traits;
		Traits::reset();
		
		{
			ConcurrentQueue<int, Traits> q;
			ProducerToken t0(q);
			
			ASSERT_OR_FAIL(t0.valid());
			
			ProducerToken t1(std::move(t0));
			ASSERT_OR_FAIL(t1.valid());
			ASSERT_OR_FAIL(!t0.valid());
			
			t1 = std::move(t1);
			ASSERT_OR_FAIL(t1.valid());
			ASSERT_OR_FAIL(!t0.valid());
			
			ProducerToken t2(q);
			t2 = std::move(t1);
			ASSERT_OR_FAIL(t2.valid());
			ASSERT_OR_FAIL(t1.valid());
			ASSERT_OR_FAIL(!t0.valid());
			
			t0 = std::move(t1);
			ASSERT_OR_FAIL(t2.valid());
			ASSERT_OR_FAIL(!t1.valid());
			ASSERT_OR_FAIL(t0.valid());
		}

		ASSERT_OR_FAIL(Traits::malloc_count() == 5);		// 2 for each producer + 1 for initial block pool
		ASSERT_OR_FAIL(Traits::free_count() == Traits::malloc_count());
		
		return true;
	}
	
	bool multi_producers()
	{
		typedef TestTraits<16> Traits;
		Traits::reset();
		
		{
			ConcurrentQueue<int, Traits> q;
			ProducerToken t0(q);
			ProducerToken t1(q);
			ProducerToken t2(q);
			ProducerToken t3(q);
			ProducerToken t4(q);
			
			ASSERT_OR_FAIL(q.enqueue(t0, 0));
			ASSERT_OR_FAIL(q.enqueue(t1, 1));
			ASSERT_OR_FAIL(q.enqueue(t2, 2));
			ASSERT_OR_FAIL(q.enqueue(t3, 3));
			ASSERT_OR_FAIL(q.enqueue(t4, 4));
			
			int item;
			ASSERT_OR_FAIL(q.try_dequeue_from_producer(t0, item) && item == 0 && !q.try_dequeue_from_producer(t0, item));
			ASSERT_OR_FAIL(q.try_dequeue_from_producer(t1, item) && item == 1 && !q.try_dequeue_from_producer(t1, item));
			ASSERT_OR_FAIL(q.try_dequeue_from_producer(t2, item) && item == 2 && !q.try_dequeue_from_producer(t2, item));
			ASSERT_OR_FAIL(q.try_dequeue_from_producer(t3, item) && item == 3 && !q.try_dequeue_from_producer(t3, item));
			ASSERT_OR_FAIL(q.try_dequeue_from_producer(t4, item) && item == 4 && !q.try_dequeue_from_producer(t4, item));
		}
		
		ASSERT_OR_FAIL(Traits::malloc_count() == 11);		// 2 for each producer + 1 for initial block pool
		ASSERT_OR_FAIL(Traits::free_count() == Traits::malloc_count());
		
		// Implicit
		Traits::reset();
		{
			ConcurrentQueue<int, Traits> q;
			std::atomic<bool> success[5];
			std::atomic<int> done(0);
			
			for (int i = 0; i != 5; ++i) {
				success[i].store(false, std::memory_order_relaxed);
			}
			
			for (int i = 0; i != 5; ++i) {
				SimpleThread t([&](int j) {
					success[j].store(q.enqueue(j), std::memory_order_relaxed);
					done.fetch_add(1, std::memory_order_release);
				}, i);
				t.join();
			}
			while (done.load(std::memory_order_acquire) != 5) {
				continue;
			}
			
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(success[i].load(std::memory_order_relaxed));
			}
			
			// Cannot rely on order that producers are added (there's a race condition), only that they are all there somewhere.
			// Also, all items may not be visible to this thread yet.
			bool itemDequeued[5] = { false, false, false, false, false };
			int item;
			for (int i = 0; i != 5;) {
				if (q.try_dequeue(item)) {
					itemDequeued[item] = true;
					++i;
				}
			}
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(itemDequeued[i]);
			}
		}
		
		ASSERT_OR_FAIL(Traits::malloc_count() <= 11 && Traits::malloc_count() >= 3);		// 2 for each producer (depending on thread ID re-use) + 1 for initial block pool
		ASSERT_OR_FAIL(Traits::free_count() == Traits::malloc_count());
		
		return true;
	}
	
	bool producer_reuse()
	{
		typedef TestTraits<16> Traits;
		
		Traits::reset();
		{
			// Explicit
			ConcurrentQueue<int, Traits> q;
			
			{
				ProducerToken t0(q);
			}
			
			{
				ProducerToken t1(q);
			}
			
			{
				ProducerToken t2(q);
				ProducerToken t3(q);
				ProducerToken t4(q);
				ProducerToken t5(q);
			}
			
			{
				ProducerToken t6(q);
				ProducerToken t7(q);
			}
			
			{
				ProducerToken t8(q);
				ProducerToken t9(q);
			}

			
			{
				ProducerToken t10(q);
				ProducerToken t11(q);
			}
		}
		
		ASSERT_OR_FAIL(Traits::malloc_count() == 9);		// 2 for max number of live producers + 1 for initial block pool
		ASSERT_OR_FAIL(Traits::free_count() == Traits::malloc_count());
		
#ifdef MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED
		Traits::reset();
		{
			// Implicit
			const int MAX_THREADS = 48;
			ConcurrentQueue<int, Traits> q(Traits::BLOCK_SIZE * (MAX_THREADS + 1));
			ASSERT_OR_FAIL(Traits::malloc_count() == 1);		// Initial block pool
			
			SimpleThread t0([&]() { q.enqueue(0); });
			t0.join();
			ASSERT_OR_FAIL(Traits::malloc_count() == 3);		// Implicit producer
			
			SimpleThread t1([&]() { q.enqueue(1); });
			t1.join();
			ASSERT_OR_FAIL(Traits::malloc_count() == 3);
			
			SimpleThread t2([&]() { q.enqueue(2); });
			t2.join();
			ASSERT_OR_FAIL(Traits::malloc_count() == 3);
			
			q.enqueue(3);
			ASSERT_OR_FAIL(Traits::malloc_count() == 3);
			
			int item;
			int i = 0;
			while (q.try_dequeue(item)) {
				ASSERT_OR_FAIL(item == i);
				++i;
			}
			ASSERT_OR_FAIL(i == 4);
			ASSERT_OR_FAIL(Traits::malloc_count() == 3);
			
			std::vector<SimpleThread> threads(MAX_THREADS);
			for (int rep = 0; rep != 2; ++rep) {
				for (std::size_t tid = 0; tid != threads.size(); ++tid) {
					threads[tid] = SimpleThread([&](std::size_t tid) {
						for (volatile int i = 0; i != 4096; ++i) {
							continue;
						}
						q.enqueue((int)tid);
						for (volatile int i = 0; i != 4096; ++i) {
							continue;
						}
					}, tid);
				}
				for (std::size_t tid = 0; tid != threads.size(); ++tid) {
					threads[tid].join();
				}
				std::vector<bool> seenIds(threads.size());
				for (std::size_t i = 0; i != threads.size(); ++i) {
					ASSERT_OR_FAIL(q.try_dequeue(item));
					ASSERT_OR_FAIL(!seenIds[item]);
					seenIds[item] = true;
				}
				for (std::size_t i = 0; i != seenIds.size(); ++i) {
					ASSERT_OR_FAIL(seenIds[i]);
				}
				ASSERT_OR_FAIL(Traits::malloc_count() <= 2 * MAX_THREADS + 1);
			}
		}
		ASSERT_OR_FAIL(Traits::free_count() == Traits::malloc_count());	
		
		
		Traits::reset();
		{
			// Test many threads and implicit queues being created and destroyed concurrently
			std::vector<SimpleThread> threads(32);
			std::vector<bool> success(threads.size(), true);
			for (std::size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid] = SimpleThread([&](std::size_t tid) {
					for (int i = 0; i != 5; ++i) {
						ConcurrentQueue<int, MallocTrackingTraits> q(1);
						q.enqueue(i);
					}
					
					ConcurrentQueue<int, MallocTrackingTraits> q(15);
					for (int i = 0; i != 100; ++i) {
						q.enqueue(i);
					}
					int item;
					for (int i = 0; i != 100; ++i) {
						if (!q.try_dequeue(item) || item != i) {
							success[tid] = false;
						}
					}
					if (q.size_approx() != 0) {
						success[tid] = false;
					}
				}, tid);
			}
			for (std::size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid].join();
				ASSERT_OR_FAIL(success[tid]);
			}
		}
		ASSERT_OR_FAIL(Traits::free_count() == Traits::malloc_count());	
#endif
		
		return true;
	}
	
	bool block_reuse()
	{
		int item;
		
		typedef TestTraits<4> SmallBlocks;
		SmallBlocks::reset();
		{
			ConcurrentQueue<int, SmallBlocks> q(8);		// 2 blocks
			ProducerToken t(q);
			
			for (int j = 0; j != 3; ++j) {
				for (int i = 0; i != 4; ++i) {
					ASSERT_OR_FAIL(q.enqueue(t, i));
				}
				for (int i = 0; i != 4; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
					ASSERT_OR_FAIL(item == i);
				}
				
				for (int i = 0; i != 8; ++i) {
					ASSERT_OR_FAIL(q.enqueue(t, i));
				}
				for (int i = 0; i != 4; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
					ASSERT_OR_FAIL(item == i);
				}
				for (int i = 0; i != 4; ++i) {
					ASSERT_OR_FAIL(q.enqueue(t, i));
				}
				for (int i = 0; i != 8; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
					ASSERT_OR_FAIL(item == ((i + 4) & 7));
				}
				
				ASSERT_OR_FAIL(!q.try_dequeue_from_producer(t, item));
			}
		}
		
		ASSERT_OR_FAIL(SmallBlocks::malloc_count() == 3);
		ASSERT_OR_FAIL(SmallBlocks::free_count() == SmallBlocks::malloc_count());
		
		
		typedef TestTraits<8192> HugeBlocks;
		HugeBlocks::reset();
		{
			ConcurrentQueue<int, HugeBlocks> q(8192 * 2);		// 2 blocks
			ProducerToken t(q);
			
			for (int j = 0; j != 3; ++j) {
				for (int i = 0; i != 8192; ++i) {
					ASSERT_OR_FAIL(q.enqueue(t, i));
				}
				for (int i = 0; i != 8192; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
					ASSERT_OR_FAIL(item == i);
				}
				
				for (int i = 0; i != 8192 * 2; ++i) {
					ASSERT_OR_FAIL(q.enqueue(t, i));
				}
				for (int i = 0; i != 8192; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
					ASSERT_OR_FAIL(item == i);
				}
				for (int i = 0; i != 8192; ++i) {
					ASSERT_OR_FAIL(q.enqueue(t, i));
				}
				for (int i = 0; i != 8192 * 2; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
					ASSERT_OR_FAIL(item == ((i + 8192) & (8192 * 2 - 1)));
				}
				
				ASSERT_OR_FAIL(!q.try_dequeue_from_producer(t, item));
			}
		}
		
		ASSERT_OR_FAIL(HugeBlocks::malloc_count() == 3);
		ASSERT_OR_FAIL(HugeBlocks::free_count() == HugeBlocks::malloc_count());
		
		
		// Implicit
		SmallBlocks::reset();
		{
			ConcurrentQueue<int, SmallBlocks> q(8);		// 2 blocks
			
			for (int j = 0; j != 3; ++j) {
				for (int i = 0; i != 4; ++i) {
					ASSERT_OR_FAIL(q.enqueue(i));
				}
				for (int i = 0; i != 4; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue(item));
					ASSERT_OR_FAIL(item == i);
				}
				
				for (int i = 0; i != 8; ++i) {
					ASSERT_OR_FAIL(q.enqueue(i));
				}
				for (int i = 0; i != 4; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue(item));
					ASSERT_OR_FAIL(item == i);
				}
				for (int i = 0; i != 4; ++i) {
					ASSERT_OR_FAIL(q.enqueue(i));
				}
				for (int i = 0; i != 8; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue(item));
					ASSERT_OR_FAIL(item == ((i + 4) & 7));
				}
				
				ASSERT_OR_FAIL(!q.try_dequeue(item));
			}
		}
		
		ASSERT_OR_FAIL(SmallBlocks::malloc_count() == 3);
		ASSERT_OR_FAIL(SmallBlocks::free_count() == SmallBlocks::malloc_count());
		
		HugeBlocks::reset();
		{
			ConcurrentQueue<int, HugeBlocks> q(8192 * 2);		// 2 blocks
			
			for (int j = 0; j != 3; ++j) {
				for (int i = 0; i != 8192; ++i) {
					ASSERT_OR_FAIL(q.enqueue(i));
				}
				for (int i = 0; i != 8192; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue(item));
					ASSERT_OR_FAIL(item == i);
				}
				
				for (int i = 0; i != 8192 * 2; ++i) {
					ASSERT_OR_FAIL(q.enqueue(i));
				}
				for (int i = 0; i != 8192; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue(item));
					ASSERT_OR_FAIL(item == i);
				}
				for (int i = 0; i != 8192; ++i) {
					ASSERT_OR_FAIL(q.enqueue(i));
				}
				for (int i = 0; i != 8192 * 2; ++i) {
					ASSERT_OR_FAIL(q.try_dequeue(item));
					ASSERT_OR_FAIL(item == ((i + 8192) & (8192 * 2 - 1)));
				}
				
				ASSERT_OR_FAIL(!q.try_dequeue(item));
			}
		}
		
		ASSERT_OR_FAIL(HugeBlocks::malloc_count() == 3);
		ASSERT_OR_FAIL(HugeBlocks::free_count() == HugeBlocks::malloc_count());
		
		return true;
	}
	
	bool block_recycling()
	{
		typedef TestTraits<4> SmallBlocks;
		SmallBlocks::reset();
		
		ConcurrentQueue<int, SmallBlocks> q(24);		// 6 blocks
		SimpleThread threads[4];
		std::atomic<bool> success(true);
		
		for (int i = 0; i != 4; ++i) {
			threads[i] = SimpleThread([&](int i) {
				int item;
				int next = 0;
				int prevItems[4] = { -1, -1, -1, -1 };
				for (int successfulEnqueues = 0; successfulEnqueues < 10000;) {
					for (int j = 0; j != 12; ++j) {
						if (q.try_enqueue((i << 28) | next++)) {
							++successfulEnqueues;
						}
					}
					for (int j = 0; j != 12; ++j) {
						if (q.try_dequeue(item)) {
							if ((item & 0x0FFFFFFF) <= prevItems[item >> 28]) {
								success.store(false, std::memory_order_relaxed);
							}
							prevItems[item >> 28] = item & 0x0FFFFFFF;
						}
					}
				}
			}, i);
		}
		for (int i = 0; i != 4; ++i) {
			threads[i].join();
		}
		
		int item;
		int prevItems[4] = { -1, -1, -1, -1 };
		while (q.try_dequeue(item)) {
			ASSERT_OR_FAIL((item & 0x0FFFFFFF) > prevItems[item >> 28]);
			prevItems[item >> 28] = item & 0x0FFFFFFF;
		}
		
		ASSERT_OR_FAIL(success.load(std::memory_order_relaxed));
		
		return true;
	}
	
	bool leftovers_destroyed()
	{
		typedef TestTraits<4> Traits;
		Traits::reset();
		Foo::reset();
		{
			ConcurrentQueue<Foo, Traits> q(4);		// One block
			ProducerToken t(q);
			
			Foo item;
			q.enqueue(t, Foo());
			q.enqueue(t, Foo());
			q.enqueue(t, Foo());
			q.try_dequeue_from_producer(t, item);
		}
		ASSERT_OR_FAIL(Foo::createCount() == 4);
		ASSERT_OR_FAIL(Foo::destroyCount() == 7);
		ASSERT_OR_FAIL(Foo::destroyedInOrder());
		
		Traits::reset();
		Foo::reset();
		{
			ConcurrentQueue<Foo, Traits> q(4);		// One block
			ProducerToken t(q);
			
			q.enqueue(t, Foo());
			q.enqueue(t, Foo());
			q.enqueue(t, Foo());
			q.enqueue(t, Foo());
		}
		ASSERT_OR_FAIL(Foo::createCount() == 4);
		ASSERT_OR_FAIL(Foo::destroyCount() == 8);
		ASSERT_OR_FAIL(Foo::destroyedInOrder());
		
		Traits::reset();
		Foo::reset();
		{
			ConcurrentQueue<Foo, Traits> q(8);		// Two blocks
			ProducerToken t(q);
			
			for (int i = 0; i != 8; ++i) {
				q.enqueue(t, Foo());
			}
		}
		ASSERT_OR_FAIL(Foo::createCount() == 8);
		ASSERT_OR_FAIL(Foo::destroyCount() == 16);
		ASSERT_OR_FAIL(Foo::destroyedInOrder());
		
		Traits::reset();
		Foo::reset();
		{
			ConcurrentQueue<Foo, Traits> q(12);		// Three blocks
			ProducerToken t(q);
			
			// Last block only partially full
			for (int i = 0; i != 10; ++i) {
				q.enqueue(t, Foo());
			}
			
			// First block only partially full
			Foo item;
			ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
			ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
			ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
		}
		ASSERT_OR_FAIL(Foo::createCount() == 11);
		ASSERT_OR_FAIL(Foo::destroyCount() == 21);
		ASSERT_OR_FAIL(Foo::destroyedInOrder());
		
		
		// Implicit
		Traits::reset();
		Foo::reset();
		{
			ConcurrentQueue<Foo, Traits> q(4);		// One block
			
			Foo item;
			q.enqueue(Foo());
			q.enqueue(Foo());
			q.enqueue(Foo());
			q.try_dequeue(item);
		}
		ASSERT_OR_FAIL(Foo::createCount() == 4);
		ASSERT_OR_FAIL(Foo::destroyCount() == 7);
		ASSERT_OR_FAIL(Foo::destroyedInOrder());
		
		Traits::reset();
		Foo::reset();
		{
			ConcurrentQueue<Foo, Traits> q(4);		// One block
			
			q.enqueue(Foo());
			q.enqueue(Foo());
			q.enqueue(Foo());
			q.enqueue(Foo());
		}
		ASSERT_OR_FAIL(Foo::createCount() == 4);
		ASSERT_OR_FAIL(Foo::destroyCount() == 8);
		ASSERT_OR_FAIL(Foo::destroyedInOrder());
		
		Traits::reset();
		Foo::reset();
		{
			ConcurrentQueue<Foo, Traits> q(8);		// Two blocks
			
			for (int i = 0; i != 8; ++i) {
				q.enqueue(Foo());
			}
		}
		ASSERT_OR_FAIL(Foo::createCount() == 8);
		ASSERT_OR_FAIL(Foo::destroyCount() == 16);
		ASSERT_OR_FAIL(Foo::destroyedInOrder());
		
		Traits::reset();
		Foo::reset();
		{
			ConcurrentQueue<Foo, Traits> q(12);		// Three blocks
			
			// Last block only partially full
			for (int i = 0; i != 10; ++i) {
				q.enqueue(Foo());
			}
			
			// First block only partially full
			Foo item;
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(q.try_dequeue(item));
		}
		ASSERT_OR_FAIL(Foo::createCount() == 11);
		ASSERT_OR_FAIL(Foo::destroyCount() == 21);
		ASSERT_OR_FAIL(Foo::destroyedInOrder());
		
		return true;
	}
	
	bool block_index_resized()
	{
		typedef TestTraits<4, 2> Traits;
		Traits::reset();
		Foo::reset();
		
		{
			ConcurrentQueue<Foo, Traits> q(8);		// 2 blocks, matches initial index size
			ProducerToken t(q);
			
			for (int i = 0; i != 1024; ++i) {
				q.enqueue(t, Foo());
			}
			
			for (int i = 0; i != 1024; ++i) {
				Foo item;
				q.try_dequeue_from_producer(t, item);
			}
		}
		
		ASSERT_OR_FAIL(Traits::malloc_count() == 1 + 2 + 254 + 7);
		ASSERT_OR_FAIL(Traits::free_count() == Traits::malloc_count());
		
		ASSERT_OR_FAIL(Foo::createCount() == 2048);
		ASSERT_OR_FAIL(Foo::destroyCount() == 3072);
		ASSERT_OR_FAIL(Foo::destroyedInOrder());
		
		// Implicit
		Traits::reset();
		Foo::reset();
		{
			ConcurrentQueue<Foo, Traits> q(8);		// 2 blocks
			
			for (int i = 0; i != 1024; ++i) {
				q.enqueue(Foo());
			}
			
			for (int i = 0; i != 1024; ++i) {
				Foo item;
				q.try_dequeue(item);
			}
		}
		
		ASSERT_OR_FAIL(Traits::malloc_count() == 1 + 2 + 254 + 6);
		ASSERT_OR_FAIL(Traits::free_count() == Traits::malloc_count());
		
		ASSERT_OR_FAIL(Foo::createCount() == 2048);
		ASSERT_OR_FAIL(Foo::destroyCount() == 3072);
		ASSERT_OR_FAIL(Foo::destroyedInOrder());
		
		return true;
	}
	
	bool try_dequeue()
	{
		ConcurrentQueue<int, MallocTrackingTraits> q;
		int item;
		
		// Producer token
		{
			for (int i = 0; i != 50; ++i) {
				ProducerToken t(q);
				for (int j = 0; j != 100; ++j) {
					ASSERT_OR_FAIL(q.enqueue(t, i * 100 + j));
				}
			}
			
			
			for (int i = 0; i != 50; ++i) {
				for (int j = 0; j != 100; ++j) {
					ASSERT_OR_FAIL(q.try_dequeue(item));
					ASSERT_OR_FAIL(item == i * 100 + j);
				}
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// Mixed producer types
		{
			for (int i = 0; i != 25; ++i) {
				for (int j = 0; j != 100; ++j) {
					ASSERT_OR_FAIL(q.enqueue(i * 100 + j));
				}
			}
			for (int i = 25; i != 50; ++i) {
				ProducerToken t(q);
				for (int j = 0; j != 100; ++j) {
					ASSERT_OR_FAIL(q.enqueue(t, i * 100 + j));
				}
			}
			bool success[5000];
			std::memset(success, 0, sizeof(success));
			for (int i = 0; i != 50; ++i) {
				for (int j = 0; j != 100; ++j) {
					ASSERT_OR_FAIL(q.try_dequeue(item));
					success[item] = true;
				}
			}
			for (int i = 0; i != 5000; ++i) {
				ASSERT_OR_FAIL(success[i]);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// Mixed producer types with consumer token
		{
			for (int i = 0; i != 25; ++i) {
				for (int j = 0; j != 100; ++j) {
					ASSERT_OR_FAIL(q.enqueue(i * 100 + j));
				}
			}
			for (int i = 25; i != 50; ++i) {
				ProducerToken t(q);
				for (int j = 0; j != 100; ++j) {
					ASSERT_OR_FAIL(q.enqueue(t, i * 100 + j));
				}
			}
			bool success[5000];
			std::memset(success, 0, sizeof(success));
			for (int i = 0; i != 50; ++i) {
				ConsumerToken t(q);
				for (int j = 0; j != 100; ++j) {
					ASSERT_OR_FAIL(q.try_dequeue(t, item));
					success[item] = true;
				}
			}
			for (int i = 0; i != 5000; ++i) {
				ASSERT_OR_FAIL(success[i]);
			}
			ConsumerToken t(q);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			ASSERT_OR_FAIL(!q.try_dequeue(t, item));
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			ASSERT_OR_FAIL(!q.try_dequeue(t, item));
		}
		
		return true;
	}
	
	bool try_dequeue_threaded()
	{
		int item;
		ConcurrentQueue<int, MallocTrackingTraits> q;
		
		// Threaded consumption with tokens
		{
			SimpleThread threads[20];
			for (int i = 0; i != 10; ++i) {
				threads[i] = SimpleThread([&](int i) {
					ProducerToken t(q);
					for (int j = 0; j != 100; ++j) {
						q.enqueue(t, i * 10 + j);
					}
				}, i);
			}

			std::atomic<int> dequeueCount(0);
			for (int i = 10; i != 20; ++i) {
				threads[i] = SimpleThread([&]() {
					int item;
					ConsumerToken t(q);
					while (dequeueCount.load(std::memory_order_relaxed) != 1000) {
						if (q.try_dequeue(t, item)) {
							dequeueCount.fetch_add(1, std::memory_order_relaxed);
						}
					}
				});
			}
			
			for (int i = 0; i != 20; ++i) {
				threads[i].join();
			}
			
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// Threaded consumption
		{
			SimpleThread threads[20];
			for (int i = 0; i != 10; ++i) {
				threads[i] = SimpleThread([&](int i) {
					for (int j = 0; j != 100; ++j) {
						q.enqueue(i * 10 + j);
					}
				}, i);
			}

			std::atomic<int> dequeueCount(0);
			for (int i = 10; i != 20; ++i) {
				threads[i] = SimpleThread([&]() {
					int item;
					while (dequeueCount.load(std::memory_order_relaxed) != 1000) {
						if (q.try_dequeue(item)) {
							dequeueCount.fetch_add(1, std::memory_order_relaxed);
						}
					}
				});
			}
			
			for (int i = 0; i != 20; ++i) {
				threads[i].join();
			}
			
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		return true;
	}
	
	bool try_dequeue_bulk()
	{
		typedef TestTraits<4> Traits;
		int items[5];
		
		// Explicit producer
		{
			Traits::reset();
			ConcurrentQueue<int, Traits> q;
			ProducerToken tok(q);
			
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 0);
			
			q.enqueue(tok, 17);
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 1);
			ASSERT_OR_FAIL(items[0] == 17);
			
			ASSERT_OR_FAIL(!q.try_dequeue(items[0]));
			
			for (int i = 0; i != 4; ++i) {
				q.enqueue(tok, i + 1);
			}
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 4);
			for (int i = 0; i != 4; ++i) {
				ASSERT_OR_FAIL(items[i] == i + 1);
			}
			
			ASSERT_OR_FAIL(!q.try_dequeue(items[0]));
			
			for (int i = 0; i != 5; ++i) {
				q.enqueue(tok, i + 1);
			}
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 5);
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(items[i] == i + 1);
			}
			
			ASSERT_OR_FAIL(!q.try_dequeue(items[0]));
			
			for (int i = 0; i != 6; ++i) {
				q.enqueue(tok, i + 1);
			}
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 5);
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(items[i] == i + 1);
			}
			ASSERT_OR_FAIL(q.try_dequeue(items[0]));
			ASSERT_OR_FAIL(items[0] == 6);
			ASSERT_OR_FAIL(!q.try_dequeue(items[0]));
			
			for (int i = 0; i != 10; ++i) {
				q.enqueue(tok, i + 1);
			}
			for (int k = 0; k != 2; ++k) {
				ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 5);
				for (int i = 0; i != 5; ++i) {
					ASSERT_OR_FAIL(items[i] == k * 5 + i + 1);
				}
			}
			ASSERT_OR_FAIL(!q.try_dequeue(items[0]));
		}
		
		// Implicit producer
		{
			Traits::reset();
			ConcurrentQueue<int, Traits> q;
			
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 0);
			
			q.enqueue(17);
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 1);
			ASSERT_OR_FAIL(items[0] == 17);
			
			ASSERT_OR_FAIL(!q.try_dequeue(items[0]));
			
			for (int i = 0; i != 4; ++i) {
				q.enqueue(i + 1);
			}
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 4);
			for (int i = 0; i != 4; ++i) {
				ASSERT_OR_FAIL(items[i] == i + 1);
			}
			
			ASSERT_OR_FAIL(!q.try_dequeue(items[0]));
			
			for (int i = 0; i != 5; ++i) {
				q.enqueue(i + 1);
			}
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 5);
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(items[i] == i + 1);
			}
			
			ASSERT_OR_FAIL(!q.try_dequeue(items[0]));
			
			for (int i = 0; i != 6; ++i) {
				q.enqueue(i + 1);
			}
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 5);
			for (int i = 0; i != 5; ++i) {
				ASSERT_OR_FAIL(items[i] == i + 1);
			}
			ASSERT_OR_FAIL(q.try_dequeue(items[0]));
			ASSERT_OR_FAIL(items[0] == 6);
			ASSERT_OR_FAIL(!q.try_dequeue(items[0]));
			
			for (int i = 0; i != 10; ++i) {
				q.enqueue(i + 1);
			}
			for (int k = 0; k != 2; ++k) {
				ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 5) == 5);
				for (int i = 0; i != 5; ++i) {
					ASSERT_OR_FAIL(items[i] == k * 5 + i + 1);
				}
			}
			ASSERT_OR_FAIL(!q.try_dequeue(items[0]));
		}
		
		return true;
	}
	
	bool try_dequeue_bulk_threaded()
	{
		typedef TestTraits<2> Traits;
		int dummy;
		
		// Explicit producer
		{
			Traits::reset();
			ConcurrentQueue<int, Traits> q;
			SimpleThread threads[2];
			bool success[2] = { true, true };
			for (int i = 0; i != 2; ++i) {
				if (i == 0) {
					threads[i] = SimpleThread([&](int) {
						// Producer
						ProducerToken tok(q);
						for (int i = 0; i != 32*1024; ++i) {
							q.enqueue(tok, i);
						}
					}, i);
				}
				else {
					threads[i] = SimpleThread([&](int) {
						// Consumer
						int items[5];
						int prevItem = -1;
						for (int i = 0; i != 32*1024;) {
							auto dequeued = q.try_dequeue_bulk(items, 5);
							if (dequeued > 0) {
								if (dequeued > 5) {
									success[i] = false;
									break;
								}
								for (std::size_t j = 0; j != dequeued; ++j) {
									if (items[j] != prevItem + 1) {
										success[i] = false;
									}
									prevItem = items[j];
								}
								i += (int)dequeued;
							}
						}
					}, i);
				}
			}
			for (int i = 0; i != 2; ++i) {
				threads[i].join();
			}
			
			ASSERT_OR_FAIL(success[0]);
			ASSERT_OR_FAIL(success[1]);
			ASSERT_OR_FAIL(!q.try_dequeue(dummy));
		}
		
		// Implicit producer
		{
			Traits::reset();
			ConcurrentQueue<int, Traits> q;
			SimpleThread threads[2];
			bool success[2] = { true, true };
			for (int i = 0; i != 2; ++i) {
				if (i == 0) {
					threads[i] = SimpleThread([&](int) {
						// Producer
						for (int i = 0; i != 32*1024; ++i) {
							q.enqueue(i);
						}
					}, i);
				}
				else {
					threads[i] = SimpleThread([&](int) {
						// Consumer
						int items[5];
						int prevItem = -1;
						for (int i = 0; i != 32*1024;) {
							auto dequeued = q.try_dequeue_bulk(items, 5);
							if (dequeued > 0) {
								if (dequeued > 5) {
									success[i] = false;
									break;
								}
								for (std::size_t j = 0; j != dequeued; ++j) {
									if (items[j] != prevItem + 1) {
										success[i] = false;
									}
									prevItem = items[j];
								}
								i += (int)dequeued;
							}
						}
					}, i);
				}
			}
			for (int i = 0; i != 2; ++i) {
				threads[i].join();
			}
			
			ASSERT_OR_FAIL(success[0]);
			ASSERT_OR_FAIL(success[1]);
			ASSERT_OR_FAIL(!q.try_dequeue(dummy));
		}
		
		// Multithreaded consumption
		{
			Traits::reset();
			ConcurrentQueue<int, Traits> q;
			
			bool success[20];
			SimpleThread threads[20];
			for (int i = 0; i != 10; ++i) {
				success[i] = true;
				threads[i] = SimpleThread([&](int i) {
					ProducerToken t(q);
					if ((i & 1) == 1) {
						for (int j = 0; j != 100; ++j) {
							q.enqueue(t, i * 128 + j);
						}
					}
					else {
						for (int j = 0; j != 100; ++j) {
							q.enqueue(i * 128 + j);
						}
					}
				}, i);
			}
			
			std::atomic<size_t> dequeueCount(0);
			for (int i = 10; i != 20; ++i) {
				success[i] = true;
				threads[i] = SimpleThread([&](int i) {
					int prevItems[10];
					for (int j = 0; j != 10; ++j) {
						prevItems[j] = -1;
					}
					int items[15];
					ConsumerToken t(q);
					
					while (dequeueCount.load(std::memory_order_relaxed) != 1000) {
						size_t count;
						if ((i & 1) == 1) {
							count = q.try_dequeue_bulk(items, 15);
						}
						else {
							count = q.try_dequeue_bulk(t, items, 15);
						}
						
						if (count > 15) {
							success[i] = false;
						}
						for (size_t k = 0; k != count; ++k) {
							if (prevItems[items[k] / 128] >= (items[k] & 127)) {
								success[i] = false;
							}
							prevItems[items[k] / 128] = items[k] & 127;
						}
						dequeueCount.fetch_add(count, std::memory_order_relaxed);
					}
				}, i);
			}
			
			for (int i = 0; i != 20; ++i) {
				threads[i].join();
			}
			
			int item;
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			for (int i = 0; i != 20; ++i) {
				ASSERT_OR_FAIL(success[i]);
			}
		}
		
		return true;
	}
	
	bool implicit_producer_hash()
	{
		for (int j = 0; j != 5; ++j) {
			ConcurrentQueue<int, MallocTrackingTraits> q;
			std::vector<SimpleThread> threads;
			for (int i = 0; i != 20; ++i) {
				threads.push_back(SimpleThread([&]() {
					q.enqueue(7);
				}));
			}
			
			for (auto it = threads.begin(); it != threads.end(); ++it) {
				it->join();
			}
			
			int item;
			ConsumerToken t(q);
			for (auto i = 0; i != 20; ++i) {
				if ((j & 1) == 0) {
					ASSERT_OR_FAIL(q.try_dequeue(item));
				}
				else {
					ASSERT_OR_FAIL(q.try_dequeue(t, item));
				}
				ASSERT_OR_FAIL(item == 7);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		return true;
	}
	
	bool index_wrapping()
	{
		{
			// Implicit
			ConcurrentQueue<int, SmallIndexTraits> q(16);
			int item;
			
			for (int i = 0; i != (1 << 18); ++i) {
				if ((i & 16) == 0) {
					ASSERT_OR_FAIL(q.try_enqueue(i));
				}
				else {
					ASSERT_OR_FAIL(q.try_dequeue(item));
					ASSERT_OR_FAIL(item == (i - 16));
				}
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		{
			// Explicit
			ConcurrentQueue<int, SmallIndexTraits> q(16);
			ProducerToken tok(q);
			int item;
			
			for (int i = 0; i != (1 << 18); ++i) {
				if ((i & 16) == 0) {
					ASSERT_OR_FAIL(q.try_enqueue(tok, i));
				}
				else {
					ASSERT_OR_FAIL(q.try_dequeue_from_producer(tok, item));
					ASSERT_OR_FAIL(item == (i - 16));
				}
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		{
			// Implicit extra small
			ConcurrentQueue<int, ExtraSmallIndexTraits> q(1);
			int item;
			
			for (int i = 0; i != 4097; ++i) {
				q.enqueue(i);
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		{
			// Explicit extra small
			ConcurrentQueue<int, ExtraSmallIndexTraits> q(1);
			ProducerToken tok(q);
			int item;
			
			for (int i = 0; i != 4097; ++i) {
				q.enqueue(tok, i);
				ASSERT_OR_FAIL(q.try_dequeue(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		return true;
	}
	
	struct SizeLimitTraits : public MallocTrackingTraits
	{
		static const size_t BLOCK_SIZE = 2;
		static const size_t MAX_SUBQUEUE_SIZE = 5;		// Will round up to 6 because of block size
	};
	
	bool subqueue_size_limit()
	{
		{
			// Explicit
			ConcurrentQueue<int, SizeLimitTraits> q;
			ProducerToken t(q);
			int item;
			
			ASSERT_OR_FAIL(q.enqueue(t, 1));
			ASSERT_OR_FAIL(q.enqueue(t, 2));
			ASSERT_OR_FAIL(q.enqueue(t, 3));
			ASSERT_OR_FAIL(q.enqueue(t, 4));
			ASSERT_OR_FAIL(q.enqueue(t, 5));
			ASSERT_OR_FAIL(q.enqueue(t, 6));
			ASSERT_OR_FAIL(!q.enqueue(t, 7));
			ASSERT_OR_FAIL(!q.enqueue(t, 8));
			
			ASSERT_OR_FAIL(q.try_dequeue(item) && item == 1);
			ASSERT_OR_FAIL(!q.enqueue(t, 7));		// Can't reuse block until it's completely empty
			ASSERT_OR_FAIL(q.try_dequeue(item) && item == 2);
			ASSERT_OR_FAIL(q.enqueue(t, 7));
			ASSERT_OR_FAIL(q.enqueue(t, 8));
			ASSERT_OR_FAIL(!q.enqueue(t, 9));
			
			ASSERT_OR_FAIL(q.try_dequeue(item) && item == 3);
			ASSERT_OR_FAIL(!q.enqueue(t, 9));
			ASSERT_OR_FAIL(q.try_dequeue(item) && item == 4);
			ASSERT_OR_FAIL(q.enqueue(t, 9));
			
			for (int i = 5; i <= 9; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item) && item == i);
			}
			ASSERT_OR_FAIL(q.enqueue(t, 10));
			ASSERT_OR_FAIL(q.try_dequeue(item) && item == 10);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			for (int i = 0; i != 6; ++i) {
				ASSERT_OR_FAIL(q.try_enqueue(t, i));
			}
			ASSERT_OR_FAIL(!q.try_enqueue(t, 7));
			ASSERT_OR_FAIL(!q.enqueue(t, 7));
			
			// Bulk
			int items[6];
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 6) == 6);
			ASSERT_OR_FAIL(!q.try_enqueue_bulk(t, items, 7));
			ASSERT_OR_FAIL(!q.enqueue_bulk(t, items, 7));
			ASSERT_OR_FAIL(q.enqueue_bulk(t, items, 6));
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 6) == 6);
			ASSERT_OR_FAIL(q.enqueue_bulk(t, items, 3));
			ASSERT_OR_FAIL(!q.enqueue_bulk(t, items, 4));
			ASSERT_OR_FAIL(q.enqueue_bulk(t, items, 3));
			ASSERT_OR_FAIL(!q.enqueue_bulk(t, items, 1));
			ASSERT_OR_FAIL(!q.enqueue(t, 100));
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 1) == 1);
			ASSERT_OR_FAIL(!q.enqueue(t, 100));
		}
		
		{
			// Implicit
			ConcurrentQueue<int, SizeLimitTraits> q;
			int item;
			
			ASSERT_OR_FAIL(q.enqueue(1));
			ASSERT_OR_FAIL(q.enqueue(2));
			ASSERT_OR_FAIL(q.enqueue(3));
			ASSERT_OR_FAIL(q.enqueue(4));
			ASSERT_OR_FAIL(q.enqueue(5));
			ASSERT_OR_FAIL(q.enqueue(6));
			ASSERT_OR_FAIL(!q.enqueue(7));
			ASSERT_OR_FAIL(!q.enqueue(8));
			
			ASSERT_OR_FAIL(q.try_dequeue(item) && item == 1);
			ASSERT_OR_FAIL(!q.enqueue(7));		// Can't reuse block until it's completely empty
			ASSERT_OR_FAIL(q.try_dequeue(item) && item == 2);
			ASSERT_OR_FAIL(q.enqueue(7));
			ASSERT_OR_FAIL(q.enqueue(8));
			ASSERT_OR_FAIL(!q.enqueue(9));
			
			ASSERT_OR_FAIL(q.try_dequeue(item) && item == 3);
			ASSERT_OR_FAIL(!q.enqueue(9));
			ASSERT_OR_FAIL(q.try_dequeue(item) && item == 4);
			ASSERT_OR_FAIL(q.enqueue(9));
			
			for (int i = 5; i <= 9; ++i) {
				ASSERT_OR_FAIL(q.try_dequeue(item) && item == i);
			}
			ASSERT_OR_FAIL(q.enqueue(10));
			ASSERT_OR_FAIL(q.try_dequeue(item) && item == 10);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			for (int i = 0; i != 6; ++i) {
				ASSERT_OR_FAIL(q.try_enqueue(i));
			}
			ASSERT_OR_FAIL(!q.try_enqueue(7));
			ASSERT_OR_FAIL(!q.enqueue(7));
			
			// Bulk
			int items[6];
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 6) == 6);
			ASSERT_OR_FAIL(!q.try_enqueue_bulk(items, 7));
			ASSERT_OR_FAIL(!q.enqueue_bulk(items, 7));
			ASSERT_OR_FAIL(q.enqueue_bulk(items, 6));
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 6) == 6);
			ASSERT_OR_FAIL(q.enqueue_bulk(items, 3));
			ASSERT_OR_FAIL(!q.enqueue_bulk(items, 4));
			ASSERT_OR_FAIL(q.enqueue_bulk(items, 3));
			ASSERT_OR_FAIL(!q.enqueue_bulk(items, 1));
			ASSERT_OR_FAIL(!q.enqueue(100));
			ASSERT_OR_FAIL(q.try_dequeue_bulk(items, 1) == 1);
			ASSERT_OR_FAIL(!q.enqueue(100));
		}
		
		return true;
	}
	
	bool exceptions()
	{
		typedef TestTraits<4, 2> Traits;
		
		{
			// Explicit, basic
			// enqueue
			ConcurrentQueue<ThrowingMovable, Traits> q;
			ProducerToken tok(q);
			
			ThrowingMovable::reset();
			
			bool threw = false;
			try {
				q.enqueue(tok, ThrowingMovable(1, true));
			}
			catch (ThrowingMovable* m) {
				threw = true;
				ASSERT_OR_FAIL(m->id == 1);
				ASSERT_OR_FAIL(m->moved);
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 0);
			
			ASSERT_OR_FAIL(q.enqueue(tok, ThrowingMovable(2)));
			ThrowingMovable result(-1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 2);
			ASSERT_OR_FAIL(result.moved);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 3);
			
			// dequeue
			ThrowingMovable::reset();
			q.enqueue(tok, ThrowingMovable(10));
			q.enqueue(tok, ThrowingMovable(11, false, true));
			q.enqueue(tok, ThrowingMovable(12));
			ASSERT_OR_FAIL(q.size_approx() == 3);
			
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 10);
			threw = false;
			try {
				q.try_dequeue(result);
			}
			catch (ThrowingMovable* m) {
				ASSERT_OR_FAIL(m->id == 11);
				threw = true;
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 1);
			
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 12);
			ASSERT_OR_FAIL(result.moved);
			
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			q.enqueue(tok, ThrowingMovable(13));
			ASSERT_OR_FAIL(q.size_approx() == 1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 13);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 8);
		}
		
		{
			// Explicit, on and off block boundaries
			// enqueue
			ConcurrentQueue<ThrowingMovable, Traits> q;
			ProducerToken tok(q);
			
			ThrowingMovable::reset();
			
			for (int i = 0; i != 3; ++i) {
				q.enqueue(tok, ThrowingMovable(i));
			}
			bool threw = false;
			try {
				q.enqueue(tok, ThrowingMovable(3, true));
			}
			catch (ThrowingMovable* m) {
				threw = true;
				ASSERT_OR_FAIL(m->id == 3);
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 3);
			
			q.enqueue(tok, ThrowingMovable(4));
			threw = false;
			try {
				q.enqueue(tok, ThrowingMovable(5, true));
			}
			catch (ThrowingMovable* m) {
				threw = true;
				ASSERT_OR_FAIL(m->id == 5);
				ASSERT_OR_FAIL(m->moved);
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 4);
			q.enqueue(tok, ThrowingMovable(6));
			
			ThrowingMovable result(-1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 0);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 2);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 4);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 6);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 12);
			
			// dequeue
			ThrowingMovable::reset();
			q.enqueue(tok, ThrowingMovable(10, false, true));
			q.enqueue(tok, ThrowingMovable(11));
			q.enqueue(tok, ThrowingMovable(12));
			q.enqueue(tok, ThrowingMovable(13, false, true));
			q.enqueue(tok, ThrowingMovable(14, false, true));
			q.enqueue(tok, ThrowingMovable(15, false, true));
			q.enqueue(tok, ThrowingMovable(16));
			ASSERT_OR_FAIL(q.size_approx() == 7);
			
			for (int i = 10; i != 17; ++i) {
				if (i == 10 || (i >= 13 && i <= 15)) {
					threw = false;
					try {
						q.try_dequeue(result);
					}
					catch (ThrowingMovable* m) {
						ASSERT_OR_FAIL(m->id == i);
						ASSERT_OR_FAIL(m->moved);
						threw = true;
					}
					ASSERT_OR_FAIL(threw);
				}
				else {
					ASSERT_OR_FAIL(q.try_dequeue(result));
					ASSERT_OR_FAIL(result.id == i);
					ASSERT_OR_FAIL(result.moved);
				}
				ASSERT_OR_FAIL(q.size_approx() == (std::uint32_t)(16 - i));
			}
			
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			q.enqueue(tok, ThrowingMovable(20));
			ASSERT_OR_FAIL(q.size_approx() == 1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 20);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 16);
		}
		
		{
			// Explicit bulk
			// enqueue
			ConcurrentQueue<ThrowingMovable, Traits> q;
			ProducerToken tok(q);
			
			ThrowingMovable::reset();
			std::vector<ThrowingMovable> items;
			items.reserve(5);
			items.push_back(ThrowingMovable(1));
			items.push_back(ThrowingMovable(2));
			items.push_back(ThrowingMovable(3));
			items.push_back(ThrowingMovable(4));
			items.push_back(ThrowingMovable(5));
			items.back().throwOnCctor = true;
			
			bool threw = false;
			try {
				q.enqueue_bulk(tok, std::make_move_iterator(items.begin()), 5);
			}
			catch (ThrowingMovable* m) {
				threw = true;
				ASSERT_OR_FAIL(m->id == 5);
				ASSERT_OR_FAIL(m->copied);
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 0);
			q.enqueue(tok, ThrowingMovable(6));
			
			threw = false;
			try {
				q.enqueue_bulk(tok, std::make_move_iterator(items.begin()), 5);
			}
			catch (ThrowingMovable* m) {
				threw = true;
				ASSERT_OR_FAIL(m->id == 5);
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 1);
			
			ThrowingMovable result(-1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 6);
			ASSERT_OR_FAIL(result.moved);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 15);
			
			// dequeue
			ThrowingMovable::reset();
			q.enqueue(tok, ThrowingMovable(10));
			q.enqueue(tok, ThrowingMovable(11));
			q.enqueue(tok, ThrowingMovable(12));
			q.enqueue(tok, ThrowingMovable(13));
			q.enqueue(tok, ThrowingMovable(14, false, true, true));		// std::back_inserter turns an assignment into a ctor call
			q.enqueue(tok, ThrowingMovable(15));
			ASSERT_OR_FAIL(q.size_approx() == 6);
			
			std::vector<ThrowingMovable> results;
			results.reserve(5);
			ASSERT_OR_FAIL(q.try_dequeue_bulk(std::back_inserter(results), 2));
			ASSERT_OR_FAIL(results.size() == 2);
			ASSERT_OR_FAIL(results[0].id == 10);
			ASSERT_OR_FAIL(results[1].id == 11);
			ASSERT_OR_FAIL(results[0].moved);
			ASSERT_OR_FAIL(results[1].moved);
			ASSERT_OR_FAIL(q.size_approx() == 4);
			threw = false;
			try {
				q.try_dequeue_bulk(std::back_inserter(results), 4);
			}
			catch (ThrowingMovable*) {
				// Note: Can't inspect thrown value since it points to an object whose construction was attempted on the vector and
				// no longer exists
				threw = true;
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 0);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			ASSERT_OR_FAIL(q.try_dequeue_bulk(std::back_inserter(results), 1) == 0);
			
			ASSERT_OR_FAIL(results.size() == 4);
			ASSERT_OR_FAIL(results[2].id == 12);
			ASSERT_OR_FAIL(results[3].id == 13);
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 12);
		}
		
		
		{
			// Implicit, basic
			// enqueue
			ConcurrentQueue<ThrowingMovable, Traits> q;
			
			ThrowingMovable::reset();
			
			bool threw = false;
			try {
				q.enqueue(ThrowingMovable(1, true));
			}
			catch (ThrowingMovable* m) {
				threw = true;
				ASSERT_OR_FAIL(m->id == 1);
				ASSERT_OR_FAIL(m->moved);
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 0);
			
			ASSERT_OR_FAIL(q.enqueue(ThrowingMovable(2)));
			ThrowingMovable result(-1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 2);
			ASSERT_OR_FAIL(result.moved);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 3);
			
			// dequeue
			ThrowingMovable::reset();
			q.enqueue(ThrowingMovable(10));
			q.enqueue(ThrowingMovable(11, false, true));
			q.enqueue(ThrowingMovable(12));
			ASSERT_OR_FAIL(q.size_approx() == 3);
			
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 10);
			threw = false;
			try {
				q.try_dequeue(result);
			}
			catch (ThrowingMovable* m) {
				ASSERT_OR_FAIL(m->id == 11);
				threw = true;
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 1);
			
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 12);
			ASSERT_OR_FAIL(result.moved);
			
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			q.enqueue(ThrowingMovable(13));
			ASSERT_OR_FAIL(q.size_approx() == 1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 13);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 8);
		}
		
		{
			// Implicit, on and off block boundaries
			// enqueue
			ConcurrentQueue<ThrowingMovable, Traits> q;
			
			ThrowingMovable::reset();
			
			for (int i = 0; i != 3; ++i) {
				q.enqueue(ThrowingMovable(i));
			}
			bool threw = false;
			try {
				q.enqueue(ThrowingMovable(3, true));
			}
			catch (ThrowingMovable* m) {
				threw = true;
				ASSERT_OR_FAIL(m->id == 3);
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 3);
			
			q.enqueue(ThrowingMovable(4));
			threw = false;
			try {
				q.enqueue(ThrowingMovable(5, true));
			}
			catch (ThrowingMovable* m) {
				threw = true;
				ASSERT_OR_FAIL(m->id == 5);
				ASSERT_OR_FAIL(m->moved);
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 4);
			q.enqueue(ThrowingMovable(6));
			
			ThrowingMovable result(-1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 0);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 2);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 4);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 6);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 12);
			
			// dequeue
			ThrowingMovable::reset();
			q.enqueue(ThrowingMovable(10, false, true));
			q.enqueue(ThrowingMovable(11));
			q.enqueue(ThrowingMovable(12));
			q.enqueue(ThrowingMovable(13, false, true));
			q.enqueue(ThrowingMovable(14, false, true));
			q.enqueue(ThrowingMovable(15, false, true));
			q.enqueue(ThrowingMovable(16));
			ASSERT_OR_FAIL(q.size_approx() == 7);
			
			for (int i = 10; i != 17; ++i) {
				if (i == 10 || (i >= 13 && i <= 15)) {
					threw = false;
					try {
						q.try_dequeue(result);
					}
					catch (ThrowingMovable* m) {
						ASSERT_OR_FAIL(m->id == i);
						ASSERT_OR_FAIL(m->moved);
						threw = true;
					}
					ASSERT_OR_FAIL(threw);
				}
				else {
					ASSERT_OR_FAIL(q.try_dequeue(result));
					ASSERT_OR_FAIL(result.id == i);
					ASSERT_OR_FAIL(result.moved);
				}
				ASSERT_OR_FAIL(q.size_approx() == (std::uint32_t)(16 - i));
			}
			
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			q.enqueue(ThrowingMovable(20));
			ASSERT_OR_FAIL(q.size_approx() == 1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 20);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 16);
		}
		
		{
			// Impplicit bulk
			// enqueue
			ConcurrentQueue<ThrowingMovable, Traits> q;
			
			ThrowingMovable::reset();
			std::vector<ThrowingMovable> items;
			items.reserve(5);
			items.push_back(ThrowingMovable(1));
			items.push_back(ThrowingMovable(2));
			items.push_back(ThrowingMovable(3));
			items.push_back(ThrowingMovable(4));
			items.push_back(ThrowingMovable(5));
			items.back().throwOnCctor = true;
			
			bool threw = false;
			try {
				q.enqueue_bulk(std::make_move_iterator(items.begin()), 5);
			}
			catch (ThrowingMovable* m) {
				threw = true;
				ASSERT_OR_FAIL(m->id == 5);
				ASSERT_OR_FAIL(m->copied);
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 0);
			q.enqueue(ThrowingMovable(6));
			
			threw = false;
			try {
				q.enqueue_bulk(std::make_move_iterator(items.begin()), 5);
			}
			catch (ThrowingMovable* m) {
				threw = true;
				ASSERT_OR_FAIL(m->id == 5);
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 1);
			
			ThrowingMovable result(-1);
			ASSERT_OR_FAIL(q.try_dequeue(result));
			ASSERT_OR_FAIL(result.id == 6);
			ASSERT_OR_FAIL(result.moved);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 15);
			
			// dequeue
			ThrowingMovable::reset();
			q.enqueue(ThrowingMovable(10));
			q.enqueue(ThrowingMovable(11));
			q.enqueue(ThrowingMovable(12));
			q.enqueue(ThrowingMovable(13));
			q.enqueue(ThrowingMovable(14, false, true, true));		// std::back_inserter turns an assignment into a ctor call
			q.enqueue(ThrowingMovable(15));
			ASSERT_OR_FAIL(q.size_approx() == 6);
			
			std::vector<ThrowingMovable> results;
			results.reserve(5);
			ASSERT_OR_FAIL(q.try_dequeue_bulk(std::back_inserter(results), 2));
			ASSERT_OR_FAIL(results.size() == 2);
			ASSERT_OR_FAIL(results[0].id == 10);
			ASSERT_OR_FAIL(results[1].id == 11);
			ASSERT_OR_FAIL(results[0].moved);
			ASSERT_OR_FAIL(results[1].moved);
			ASSERT_OR_FAIL(q.size_approx() == 4);
			threw = false;
			try {
				q.try_dequeue_bulk(std::back_inserter(results), 4);
			}
			catch (ThrowingMovable*) {
				threw = true;
			}
			ASSERT_OR_FAIL(threw);
			ASSERT_OR_FAIL(q.size_approx() == 0);
			ASSERT_OR_FAIL(!q.try_dequeue(result));
			ASSERT_OR_FAIL(q.try_dequeue_bulk(std::back_inserter(results), 1) == 0);
			
			ASSERT_OR_FAIL(results.size() == 4);
			ASSERT_OR_FAIL(results[2].id == 12);
			ASSERT_OR_FAIL(results[3].id == 13);
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() == 12);
		}
		
		{
			// Threaded
			ConcurrentQueue<ThrowingMovable, Traits> q;
			ThrowingMovable::reset();
			
			std::vector<SimpleThread> threads(6);
			for (std::size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid] = SimpleThread([&](std::size_t tid) {
					std::vector<ThrowingMovable> inVec;
					inVec.push_back(ThrowingMovable(1));
					inVec.push_back(ThrowingMovable(2));
					inVec.push_back(ThrowingMovable(3));
					
					std::vector<ThrowingMovable> outVec;
					outVec.push_back(ThrowingMovable(-1));
					outVec.push_back(ThrowingMovable(-1));
					outVec.push_back(ThrowingMovable(-1));
					
					ProducerToken tok(q);
					ThrowingMovable result(-1);
					
					for (std::size_t i = 0; i != 8192; ++i) {
						auto magic = (tid + 1) * i + tid * 17 + i;
						auto op = magic & 7;
						auto ctorThrow = (magic & 0x10) != 0;
						auto assignThrow = (magic & 0x20) != 0;
						auto throwOnNextCctor = (magic & 0x40) != 0;
						try {
							switch (op) {
							case 0:
								q.enqueue(tok, ThrowingMovable((int)i, ctorThrow, assignThrow, throwOnNextCctor));
								break;
							case 1:
								inVec[i & 3].throwOnCctor = ctorThrow;
								inVec[i & 3].throwOnAssignment = assignThrow;
								inVec[i & 3].throwOnSecondCctor = throwOnNextCctor;
								q.enqueue_bulk(tok, inVec.begin(), 3);
								break;
							case 2:
								q.enqueue(ThrowingMovable((int)i, ctorThrow, assignThrow, throwOnNextCctor));
								break;
							case 3:
								inVec[i & 3].throwOnCctor = ctorThrow;
								inVec[i & 3].throwOnAssignment = assignThrow;
								inVec[i & 3].throwOnSecondCctor = throwOnNextCctor;
								q.enqueue_bulk(inVec.begin(), 3);
								break;
							case 4:
							case 5:
								q.try_dequeue(result);
								break;
							case 6:
							case 7:
								q.try_dequeue_bulk(outVec.data(), 3);
								break;
							}
						}
						catch (ThrowingMovable*) {
						}
					}
				}, tid);
			}
			for (std::size_t i = 0; i != threads.size(); ++i) {
				threads[i].join();
			}
			
			ThrowingMovable result(-1);
			while (true) {
				try {
					if (!q.try_dequeue(result)) {
						break;
					}
				}
				catch (ThrowingMovable*) {
				}
			}
			
			ASSERT_OR_FAIL(ThrowingMovable::destroyCount() + 1 == ThrowingMovable::ctorCount());
		}
		
		return true;
	}

	bool implicit_producer_churn()
	{
		typedef TestTraits<4> Traits;

		for (int i = 0; i != 256; ++i) {
			std::vector<SimpleThread> threads(32);
			ConcurrentQueue<int, Traits> q;
			for (auto& thread : threads) {
				thread = SimpleThread([&] {
					int x;
					for (int j = 0; j != 16; ++j) {
						q.enqueue(0);
						q.try_dequeue(x);
					}
				});
			}
			for (auto& thread : threads) {
				thread.join();
			}
		}
		return true;
	}
	
	bool test_threaded()
	{
		typedef TestTraits<4> Traits;
		Traits::reset();
		
		bool inOrder = true;
		
		{
			// Single producer, single consumer
			ConcurrentQueue<int, Traits> q;
			ProducerToken t(q);
			SimpleThread a([&]() {
				for (int i = 0; i != 123456; ++i) {
					q.enqueue(t, i);
				}
			});
			SimpleThread b([&]() {
				int item;
				int prevItem = -1;
				while (true) {
					if (q.try_dequeue_from_producer(t, item)) {
						if (item == 123455) {
							break;
						}
						inOrder = item == prevItem + 1 && inOrder;
						prevItem = item;
					}
				}
			});
			
			a.join();
			b.join();
		}
		ASSERT_OR_FAIL(inOrder);
		
		{
			// Single producer, multi consumer
			ConcurrentQueue<int, Traits> q;
			ProducerToken t(q);
			SimpleThread a([&]() {
				for (int i = 0; i != 123456; ++i) {
					q.enqueue(t, i);
				}
			});
			SimpleThread b([&]() {
				int item, prevItem = -1;
				for (int i = 0; i != 123456; ++i) {
					if (q.try_dequeue_from_producer(t, item)) {
						inOrder = item > prevItem && inOrder;
						prevItem = item;
					}
				}
			});
			SimpleThread c([&]() {
				int item;
				for (int i = 0; i != 123456; ++i) q.try_dequeue_from_producer(t, item);
			});
			SimpleThread d([&]() {
				int item;
				for (int i = 0; i != 123456; ++i) q.try_dequeue_from_producer(t, item);
			});
			
			a.join();
			b.join();
			c.join();
			d.join();
		}
		ASSERT_OR_FAIL(inOrder);
		
		ASSERT_OR_FAIL(Traits::malloc_count() == Traits::free_count());
		
		return true;
	}
	
	bool test_threaded_bulk()
	{
		typedef TestTraits<2> Traits;
		
		// Enqueue bulk (implicit)
		Traits::reset();
		{
			ConcurrentQueue<int, Traits> q;
			SimpleThread threads[2];
			bool success[2];
			
			int stuff[] = { 1, 2, 3, 4, 5 };
			for (int i = 0; i != 2; ++i) {
				success[i] = true;
				
				if (i == 0) {
					// Enqueue bulk
					threads[i] = SimpleThread([&](int j) {
						for (int k = 0; k != 2048; ++k) {
							success[j] = q.enqueue_bulk(stuff, 5) && success[j];
						}
					}, i);
				}
				else {
					// Dequeue
					threads[i] = SimpleThread([&](int j) {
						int item;
						int prevItem = 0;
						for (int k = 0; k != 2048 * 5;) {
							if (q.try_dequeue(item)) {
								if (item != prevItem + 1) {
									success[j] = false;
								}
								prevItem = item;
								if (item == 5) {
									prevItem = 0;
								}
								++k;
							}
						}
					}, i);
				}
			}
			for (int i = 0; i != 2; ++i) {
				threads[i].join();
			}
			
			ASSERT_OR_FAIL(success[0]);
			ASSERT_OR_FAIL(success[1]);
		}
		
		// Enqueue bulk (while somebody is dequeueing (with tokens))
		Traits::reset();
		{
			ConcurrentQueue<int, Traits> q;
			SimpleThread threads[2];
			bool success[2];
			
			int stuff[] = { 1, 2, 3, 4, 5 };
			for (int i = 0; i != 2; ++i) {
				success[i] = true;
				
				if (i == 0) {
					// Enqueue bulk
					threads[i] = SimpleThread([&](int j) {
						ProducerToken tok(q);
						for (int k = 0; k != 2048; ++k) {
							success[j] = q.enqueue_bulk(tok, stuff, 5) && success[j];
						}
					}, i);
				}
				else {
					// Dequeue
					threads[i] = SimpleThread([&](int j) {
						ConsumerToken tok(q);
						int item;
						int prevItem = 0;
						for (int k = 0; k != 2048 * 5;) {
							if (q.try_dequeue(tok, item)) {
								if (item != prevItem + 1) {
									success[j] = false;
								}
								prevItem = item;
								if (item == 5) {
									prevItem = 0;
								}
								++k;
							}
						}
					}, i);
				}
			}
			for (int i = 0; i != 2; ++i) {
				threads[i].join();
			}
			
			ASSERT_OR_FAIL(success[0]);
			ASSERT_OR_FAIL(success[1]);
		}
		
		return true;
	}
	
	template<typename Traits>
	bool full_api()
	{
		// A simple test that exercises the full public API (just to make sure every function is implemented
		// and works on at least the most basic level)
		
		// enqueue(T const&)
		{
			ConcurrentQueue<Copyable, Traits> q;
			Copyable original(12345);
			ASSERT_OR_FAIL(q.enqueue(original));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// enqueue(T&&)
		{
			ConcurrentQueue<Moveable, Traits> q;
			ASSERT_OR_FAIL(q.enqueue(Moveable(12345)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			Moveable original(12345);
			ASSERT_OR_FAIL(q.enqueue(std::move(original)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Copyable, Traits> q;
			ASSERT_OR_FAIL(q.enqueue(Copyable(12345)));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// enqueue(Token, T const&)
		{
			ConcurrentQueue<Copyable, Traits> q;
			ProducerToken t(q);
			Copyable original(12345);
			ASSERT_OR_FAIL(q.enqueue(t, original));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// enqueue(Token, T&&)
		{
			ConcurrentQueue<Moveable, Traits> q;
			ProducerToken t(q);
			ASSERT_OR_FAIL(q.enqueue(t, Moveable(12345)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			ProducerToken t(q);
			Moveable original(12345);
			ASSERT_OR_FAIL(q.enqueue(t, std::move(original)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Copyable, Traits> q;
			ProducerToken t(q);
			ASSERT_OR_FAIL(q.enqueue(t, Copyable(12345)));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// try_enqueue(T const&)
		{
			ConcurrentQueue<Copyable, Traits> q;
			Copyable original(12345);
			ASSERT_OR_FAIL(q.try_enqueue(original));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// try_enqueue(T&&)
		{
			ConcurrentQueue<Moveable, Traits> q;
			ASSERT_OR_FAIL(q.try_enqueue(Moveable(12345)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			Moveable original(12345);
			ASSERT_OR_FAIL(q.try_enqueue(std::move(original)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Copyable, Traits> q;
			ASSERT_OR_FAIL(q.try_enqueue(Copyable(12345)));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// try_enqueue(Token, T const&)
		{
			ConcurrentQueue<Copyable, Traits> q;
			ProducerToken t(q);
			Copyable original(12345);
			ASSERT_OR_FAIL(q.try_enqueue(t, original));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// try_enqueue(Token, T&&)
		{
			ConcurrentQueue<Moveable, Traits> q;
			ProducerToken t(q);
			ASSERT_OR_FAIL(q.try_enqueue(t, Moveable(12345)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			ProducerToken t(q);
			Moveable original(12345);
			ASSERT_OR_FAIL(q.try_enqueue(t, std::move(original)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Copyable, Traits> q;
			ProducerToken t(q);
			ASSERT_OR_FAIL(q.try_enqueue(t, Copyable(12345)));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// enqueue_bulk(It itemFirst, size_t count)
		{
			ConcurrentQueue<Copyable, Traits> q;
			Copyable original(12345);
			ASSERT_OR_FAIL(q.enqueue_bulk(&original, 1));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			Moveable original(12345);
			ASSERT_OR_FAIL(q.enqueue_bulk(std::make_move_iterator(&original), 1));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// enqueue_bulk(Token, It itemFirst, size_t count)
		{
			ConcurrentQueue<Copyable, Traits> q;
			ProducerToken t(q);
			Copyable original(12345);
			ASSERT_OR_FAIL(q.enqueue_bulk(t, &original, 1));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			ProducerToken t(q);
			Moveable original(12345);
			ASSERT_OR_FAIL(q.enqueue_bulk(t, std::make_move_iterator(&original), 1));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// try_enqueue_bulk(It itemFirst, size_t count)
		{
			ConcurrentQueue<Copyable, Traits> q;
			Copyable original(12345);
			ASSERT_OR_FAIL(q.try_enqueue_bulk(&original, 1));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			Moveable original(12345);
			ASSERT_OR_FAIL(q.try_enqueue_bulk(std::make_move_iterator(&original), 1));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// try_enqueue_bulk(Token, It itemFirst, size_t count)
		{
			ConcurrentQueue<Copyable, Traits> q;
			ProducerToken t(q);
			Copyable original(12345);
			ASSERT_OR_FAIL(q.try_enqueue_bulk(t, &original, 1));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			ProducerToken t(q);
			Moveable original(12345);
			ASSERT_OR_FAIL(q.try_enqueue_bulk(t, std::make_move_iterator(&original), 1));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// try_dequeue(T&)
		{
			ConcurrentQueue<Copyable, Traits> q;
			ASSERT_OR_FAIL(q.enqueue(Copyable(12345)));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			ASSERT_OR_FAIL(q.enqueue(Moveable(12345)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// try_dequeue(Token, T&)
		{
			ConcurrentQueue<Copyable, Traits> q;
			ASSERT_OR_FAIL(q.enqueue(Copyable(12345)));
			Copyable item(0);
			ConsumerToken t(q);
			ASSERT_OR_FAIL(q.try_dequeue(t, item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(t, item));
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			ASSERT_OR_FAIL(q.enqueue(Moveable(12345)));
			Moveable item(0);
			ConsumerToken t(q);
			ASSERT_OR_FAIL(q.try_dequeue(t, item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue(t, item));
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// try_dequeue_from_producer(Token, T&)
		{
			ConcurrentQueue<Copyable, Traits> q;
			ProducerToken t(q);
			ASSERT_OR_FAIL(q.enqueue(t, Copyable(12345)));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue_from_producer(t, item));
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			ProducerToken t(q);
			ASSERT_OR_FAIL(q.enqueue(t, Moveable(12345)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue_from_producer(t, item));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue_from_producer(t, item));
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// try_dequeue_bulk(T&)
		{
			ConcurrentQueue<Copyable, Traits> q;
			ASSERT_OR_FAIL(q.enqueue(Copyable(12345)));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue_bulk(&item, 1) == 1);
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue_bulk(&item, 1));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			ASSERT_OR_FAIL(q.enqueue(Moveable(12345)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue_bulk(&item, 1) == 1);
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue_bulk(&item, 1));
		}
		
		// try_dequeue_bulk(Token, T&)
		{
			ConcurrentQueue<Copyable, Traits> q;
			ASSERT_OR_FAIL(q.enqueue(Copyable(12345)));
			Copyable item(0);
			ConsumerToken t(q);
			ASSERT_OR_FAIL(q.try_dequeue_bulk(t, &item, 1));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue_bulk(t, &item, 1));
			ASSERT_OR_FAIL(!q.try_dequeue_bulk(&item, 1));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			ASSERT_OR_FAIL(q.enqueue(Moveable(12345)));
			Moveable item(0);
			ConsumerToken t(q);
			ASSERT_OR_FAIL(q.try_dequeue_bulk(t, &item, 1));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue_bulk(t, &item, 1));
			ASSERT_OR_FAIL(!q.try_dequeue_bulk(&item, 1));
		}
		
		// try_dequeue_bulk_from_producer(Token, T&)
		{
			ConcurrentQueue<Copyable, Traits> q;
			ProducerToken t(q);
			ASSERT_OR_FAIL(q.enqueue(t, Copyable(12345)));
			Copyable item(0);
			ASSERT_OR_FAIL(q.try_dequeue_bulk_from_producer(t, &item, 1));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue_bulk_from_producer(t, &item, 1));
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		{
			ConcurrentQueue<Moveable, Traits> q;
			ProducerToken t(q);
			ASSERT_OR_FAIL(q.enqueue(t, Moveable(12345)));
			Moveable item(0);
			ASSERT_OR_FAIL(q.try_dequeue_bulk_from_producer(t, &item, 1));
			ASSERT_OR_FAIL(item.id == 12345);
			ASSERT_OR_FAIL(item.moved);
			ASSERT_OR_FAIL(!item.copied);
			ASSERT_OR_FAIL(!q.try_dequeue_bulk_from_producer(t, &item, 1));
			ASSERT_OR_FAIL(!q.try_dequeue(item));
		}
		
		// size_approx()
		{
			ConcurrentQueue<Foo, Traits> q;
			for (int i = 0; i != 1234; ++i) {
				q.enqueue(Foo());
			}
			ASSERT_OR_FAIL(q.size_approx() == 1234);
		}
		
		// is_lock_free()
		{
			constexpr bool lockFree = ConcurrentQueue<Foo, Traits>::is_lock_free();
#if defined(__amd64__) || defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__) || defined(_M_PPC) || defined(__powerpc__)
			static_assert(lockFree, "is_lock_free should be true");
#else
			(void)lockFree;
#endif
		}
		
		// moving
		{
			ConcurrentQueue<int, MallocTrackingTraits> q(4);
			ProducerToken t(q);
			for (int i = 0; i != 1233; ++i) {
				q.enqueue(i);
			}
			for (int i = 1234; i != 5678; ++i) {
				q.enqueue(t, i);
			}
			ASSERT_OR_FAIL(q.size_approx() == 5677);
			
			ConcurrentQueue<int, MallocTrackingTraits> q2(std::move(q));
			ASSERT_OR_FAIL(q.size_approx() == 0);
			ASSERT_OR_FAIL(q2.size_approx() == 5677);
			
			q2.enqueue(t, 5678);
			q2.enqueue(1233);
			ASSERT_OR_FAIL(q2.size_approx() == 5679);
			
			for (int i = 1234; i != 0; --i) {
				q.enqueue(i);
			}
			ASSERT_OR_FAIL(q.size_approx() == 1234);
			
			int item;
			for (int i = 0; i <= 5678; ++i) {
				ASSERT_OR_FAIL(q2.try_dequeue_non_interleaved(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q2.try_dequeue_non_interleaved(item));
			ASSERT_OR_FAIL(q2.size_approx() == 0);
			
			for (int i = 1234; i != 0; --i) {
				ASSERT_OR_FAIL(q.try_dequeue_non_interleaved(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q.try_dequeue_non_interleaved(item));
			ASSERT_OR_FAIL(q.size_approx() == 0);
		}
		
		// swapping
		{
			ConcurrentQueue<int, MallocTrackingTraits> q1, q2, q3;
			ProducerToken t1(q1), t2(q2), t3(q3);
			
			for (int i = 1234; i != 5678; ++i) {
				q1.enqueue(t1, i);
			}
			for (int i = 21234; i != 25678; ++i) {
				q2.enqueue(t2, i);
			}
			for (int i = 31234; i != 35678; ++i) {
				q3.enqueue(t3, i);
			}
			
			for (int i = 0; i != 1234; ++i) {
				q1.enqueue(i);
			}
			for (int i = 20000; i != 21234; ++i) {
				q2.enqueue(i);
			}
			for (int i = 30000; i != 31234; ++i) {
				q3.enqueue(i);
			}
			
			{
				ConcurrentQueue<int, MallocTrackingTraits> temp;
				temp = std::move(q1);
				q1 = std::move(q2);
				q2 = std::move(temp);
			}
			// q1 in q2, q2 in q1
			
			swap(q2, q3);	// q1 in q3, q3 in q2
			q1.swap(q2);	// q2 in q2, q3 in q1
			q1.swap(q2);	// q3 in q2, q2 in q1
			q1.swap(q2);	// q2 in q2, q3 in q1
			q2.swap(q3);	// q1 in q2, q2 in q3
			
			// So now q1 is in q2, q2 is in q3, and q3 is in q1
			int item;
			for (int i = 30000; i != 35678; ++i) {
				ASSERT_OR_FAIL(q1.try_dequeue_non_interleaved(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q1.try_dequeue_non_interleaved(item));
			ASSERT_OR_FAIL(q1.size_approx() == 0);
			
			for (int i = 0; i != 5678; ++i) {
				ASSERT_OR_FAIL(q2.try_dequeue_non_interleaved(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q2.try_dequeue_non_interleaved(item));
			ASSERT_OR_FAIL(q2.size_approx() == 0);
			
			for (int i = 20000; i != 25678; ++i) {
				ASSERT_OR_FAIL(q3.try_dequeue_non_interleaved(item));
				ASSERT_OR_FAIL(item == i);
			}
			ASSERT_OR_FAIL(!q3.try_dequeue_non_interleaved(item));
			ASSERT_OR_FAIL(q3.size_approx() == 0);
		}
		
		return true;
	}
	
	
	bool blocking_wrappers()
	{
		typedef BlockingConcurrentQueue<int, MallocTrackingTraits> Q;
		ASSERT_OR_FAIL((Q::is_lock_free() == ConcurrentQueue<int, MallocTrackingTraits>::is_lock_free()));
		
		// Moving
		{
			Q a, b, c;
			a = std::move(b);
			b = std::move(c);
			a = std::move(a);
			c = std::move(b);
			b = Q(std::move(b));
			using std::swap;
			swap(a, b);
			a.swap(c);
			c.swap(c);
		}
		
		// Implicit
		{
			Q q;
			ASSERT_OR_FAIL(q.enqueue(1));
			ASSERT_OR_FAIL(q.size_approx() == 1);
			int item;
			ASSERT_OR_FAIL(q.try_dequeue(item));
			ASSERT_OR_FAIL(item == 1);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			ASSERT_OR_FAIL(q.size_approx() == 0);
			
			ASSERT_OR_FAIL(q.enqueue(2));
			ASSERT_OR_FAIL(q.enqueue(3));
			ASSERT_OR_FAIL(q.size_approx() == 2);
			q.wait_dequeue(item);
			ASSERT_OR_FAIL(item == 2);
			ASSERT_OR_FAIL(q.size_approx() == 1);
			q.wait_dequeue(item);
			ASSERT_OR_FAIL(item == 3);
			ASSERT_OR_FAIL(!q.try_dequeue(item));
			ASSERT_OR_FAIL(q.size_approx() == 0);
		}
		
		// Implicit threaded
		{
			Q q;
			const int THREADS = 8;
			SimpleThread threads[THREADS];
			bool success[THREADS];
			
			for (int i = 0; i != THREADS; ++i) {
				success[i] = true;
				
				if (i % 2 == 0) {
					// Enqueue
					if (i % 4 == 0) {
						threads[i] = SimpleThread([&](int j) {
							int stuff[5];
							for (int k = 0; k != 2048; ++k) {
								for (int x = 0; x != 5; ++x) {
									stuff[x] = (j << 16) | (k * 5 + x);
								}
								success[j] = q.enqueue_bulk(stuff, 5) && success[j];
							}
						}, i);
					}
					else {
						threads[i] = SimpleThread([&](int j) {
							for (int k = 0; k != 4096; ++k) {
								success[j] = q.enqueue((j << 16) | k) && success[j];
							}
						}, i);
					}
				}
				else {
					// Dequeue
					threads[i] = SimpleThread([&](int j) {
						int item;
						std::vector<int> prevItems(THREADS, -1);
						if (j % 4 == 1) {
							for (int k = 0; k != 2048 * 5; ++k) {
								if (q.try_dequeue(item)) {
									int thread = item >> 16;
									item &= 0xffff;
									if (item <= prevItems[thread]) {
										success[j] = false;
									}
									prevItems[thread] = item;
								}
							}
						}
						else {
							int items[6];
							for (int k = 0; k < 4096;  ++k) {
								if (std::size_t dequeued = q.try_dequeue_bulk(items, 6)) {
									for (std::size_t x = 0; x != dequeued; ++x) {
										item = items[x];
										int thread = item >> 16;
										item &= 0xffff;
										if (item <= prevItems[thread]) {
											success[j] = false;
										}
										prevItems[thread] = item;
									}
								}
							}
						}
					}, i);
				}
			}
			for (int i = 0; i != THREADS; ++i) {
				threads[i].join();
			}
			
			for (int i = 0; i != THREADS; ++i) {
				ASSERT_OR_FAIL(success[i]);
			}
		}
		
		// Implicit threaded, blocking
		{
			Q q;
			const int THREADS = 8;
			SimpleThread threads[THREADS];
			bool success[THREADS];
			
			for (int i = 0; i != THREADS; ++i) {
				success[i] = true;
				
				if (i % 2 == 0) {
					// Enqueue
					if (i % 4 == 0) {
						threads[i] = SimpleThread([&](int j) {
							int stuff[5];
							for (int k = 0; k != 2048; ++k) {
								for (int x = 0; x != 5; ++x) {
									stuff[x] = (j << 16) | (k * 5 + x);
								}
								success[j] = q.enqueue_bulk(stuff, 5) && success[j];
							}
						}, i);
					}
					else {
						threads[i] = SimpleThread([&](int j) {
							for (int k = 0; k != 4096; ++k) {
								success[j] = q.enqueue((j << 16) | k) && success[j];
							}
						}, i);
					}
				}
				else {
					// Dequeue
					threads[i] = SimpleThread([&](int j) {
						int item;
						std::vector<int> prevItems(THREADS, -1);
						if (j % 4 == 1) {
							for (int k = 0; k != 2048 * 5; ++k) {
								q.wait_dequeue(item);
								int thread = item >> 16;
								item &= 0xffff;
								if (item <= prevItems[thread]) {
									success[j] = false;
								}
								prevItems[thread] = item;
							}
						}
						else {
							int items[6];
							int k;
							for (k = 0; k < 4090; ) {
								if (std::size_t dequeued = q.wait_dequeue_bulk(items, 6)) {
									for (std::size_t x = 0; x != dequeued; ++x) {
										item = items[x];
										int thread = item >> 16;
										item &= 0xffff;
										if (item <= prevItems[thread]) {
											success[j] = false;
										}
										prevItems[thread] = item;
									}
									k += (int)dequeued;
								}
								else {
									success[j] = false;
								}
							}
							for (; k != 4096; ++k) {
								q.wait_dequeue(item);
								int thread = item >> 16;
								item &= 0xffff;
								if (item <= prevItems[thread]) {
									success[j] = false;
								}
								prevItems[thread] = item;
							}
						}
					}, i);
				}
			}
			for (int i = 0; i != THREADS; ++i) {
				threads[i].join();
			}
			
			for (int i = 0; i != THREADS; ++i) {
				ASSERT_OR_FAIL(success[i]);
			}
			ASSERT_OR_FAIL(q.size_approx() == 0);
		}
		
		// Explicit
		{
			Q q;
			ProducerToken pt(q);
			ASSERT_OR_FAIL(q.enqueue(pt, 1));
			ASSERT_OR_FAIL(q.size_approx() == 1);
			int item;
			ConsumerToken ct(q);
			ASSERT_OR_FAIL(q.try_dequeue(ct, item));
			ASSERT_OR_FAIL(item == 1);
			ASSERT_OR_FAIL(!q.try_dequeue(ct, item));
			ASSERT_OR_FAIL(q.size_approx() == 0);
			
			ASSERT_OR_FAIL(q.enqueue(pt, 2));
			ASSERT_OR_FAIL(q.enqueue(pt, 3));
			ASSERT_OR_FAIL(q.size_approx() == 2);
			q.wait_dequeue(ct, item);
			ASSERT_OR_FAIL(item == 2);
			ASSERT_OR_FAIL(q.size_approx() == 1);
			q.wait_dequeue(ct, item);
			ASSERT_OR_FAIL(item == 3);
			ASSERT_OR_FAIL(!q.try_dequeue(ct, item));
			ASSERT_OR_FAIL(q.size_approx() == 0);
		}
		
		// Explicit threaded
		{
			Q q;
			const int THREADS = 8;
			SimpleThread threads[THREADS];
			bool success[THREADS];
			
			for (int i = 0; i != THREADS; ++i) {
				success[i] = true;
				
				if (i % 2 == 0) {
					// Enqueue
					if (i % 4 == 0) {
						threads[i] = SimpleThread([&](int j) {
							ProducerToken t(q);
							int stuff[5];
							for (int k = 0; k != 2048; ++k) {
								for (int x = 0; x != 5; ++x) {
									stuff[x] = (j << 16) | (k * 5 + x);
								}
								success[j] = q.enqueue_bulk(t, stuff, 5) && success[j];
							}
						}, i);
					}
					else {
						threads[i] = SimpleThread([&](int j) {
							ProducerToken t(q);
							for (int k = 0; k != 4096; ++k) {
								success[j] = q.enqueue(t, (j << 16) | k) && success[j];
							}
						}, i);
					}
				}
				else {
					// Dequeue
					threads[i] = SimpleThread([&](int j) {
						ConsumerToken t(q);
						int item;
						std::vector<int> prevItems(THREADS, -1);
						if (j % 4 == 1) {
							for (int k = 0; k != 2048 * 5; ++k) {
								if (q.try_dequeue(t, item)) {
									int thread = item >> 16;
									item &= 0xffff;
									if (item <= prevItems[thread]) {
										success[j] = false;
									}
									prevItems[thread] = item;
								}
							}
						}
						else {
							int items[6];
							for (int k = 0; k < 4096;  ++k) {
								if (std::size_t dequeued = q.try_dequeue_bulk(t, items, 6)) {
									for (std::size_t x = 0; x != dequeued; ++x) {
										item = items[x];
										int thread = item >> 16;
										item &= 0xffff;
										if (item <= prevItems[thread]) {
											success[j] = false;
										}
										prevItems[thread] = item;
									}
								}
							}
						}
					}, i);
				}
			}
			for (int i = 0; i != THREADS; ++i) {
				threads[i].join();
			}
			
			for (int i = 0; i != THREADS; ++i) {
				ASSERT_OR_FAIL(success[i]);
			}
		}
		
		// Explicit threaded, blocking
		{
			Q q;
			const int THREADS = 8;
			SimpleThread threads[THREADS];
			bool success[THREADS];
			
			for (int i = 0; i != THREADS; ++i) {
				success[i] = true;
				
				if (i % 2 == 0) {
					// Enqueue
					if (i % 4 == 0) {
						threads[i] = SimpleThread([&](int j) {
							ProducerToken t(q);
							int stuff[5];
							for (int k = 0; k != 2048; ++k) {
								for (int x = 0; x != 5; ++x) {
									stuff[x] = (j << 16) | (k * 5 + x);
								}
								success[j] = q.enqueue_bulk(t, stuff, 5) && success[j];
							}
						}, i);
					}
					else {
						threads[i] = SimpleThread([&](int j) {
							ProducerToken t(q);
							for (int k = 0; k != 4096; ++k) {
								success[j] = q.enqueue(t, (j << 16) | k) && success[j];
							}
						}, i);
					}
				}
				else {
					// Dequeue
					threads[i] = SimpleThread([&](int j) {
						ConsumerToken t(q);
						int item;
						std::vector<int> prevItems(THREADS, -1);
						if (j % 4 == 1) {
							for (int k = 0; k != 2048 * 5; ++k) {
								q.wait_dequeue(t, item);
								int thread = item >> 16;
								item &= 0xffff;
								if (item <= prevItems[thread]) {
									success[j] = false;
								}
								prevItems[thread] = item;
							}
						}
						else {
							int items[6];
							int k;
							for (k = 0; k < 4090; ) {
								if (std::size_t dequeued = q.wait_dequeue_bulk(t, items, 6)) {
									for (std::size_t x = 0; x != dequeued; ++x) {
										item = items[x];
										int thread = item >> 16;
										item &= 0xffff;
										if (item <= prevItems[thread]) {
											success[j] = false;
										}
										prevItems[thread] = item;
									}
									k += (int)dequeued;
								}
								else {
									success[j] = false;
								}
							}
							for (; k != 4096; ++k) {
								q.wait_dequeue(t, item);
								int thread = item >> 16;
								item &= 0xffff;
								if (item <= prevItems[thread]) {
									success[j] = false;
								}
								prevItems[thread] = item;
							}
						}
					}, i);
				}
			}
			for (int i = 0; i != THREADS; ++i) {
				threads[i].join();
			}
			
			for (int i = 0; i != THREADS; ++i) {
				ASSERT_OR_FAIL(success[i]);
			}
			ASSERT_OR_FAIL(q.size_approx() == 0);
		}
		
		return true;
	}
	
	bool timed_blocking_wrappers()
	{
		typedef BlockingConcurrentQueue<int, MallocTrackingTraits> Q;
		
		// Implicit
		{
			Q q;
			int item;
			ASSERT_OR_FAIL(!q.wait_dequeue_timed(item, 0));
			ASSERT_OR_FAIL(!q.wait_dequeue_timed(item, 1));
			ASSERT_OR_FAIL(!q.wait_dequeue_timed(item, 100));
			ASSERT_OR_FAIL(!q.wait_dequeue_timed(item, std::chrono::milliseconds(1)));
			q.enqueue(123);
			ASSERT_OR_FAIL(q.wait_dequeue_timed(item, 0));
			ASSERT_OR_FAIL(item == 123);
		}
		
		// Implicit, threaded
		{
			Q q;
			const int THREADS = 8;
			SimpleThread threads[THREADS];
			bool success[THREADS];
			
			for (int i = 0; i != THREADS; ++i) {
				success[i] = true;
				
				if (i % 2 == 0) {
					// Enqueue
					if (i % 4 == 0) {
						threads[i] = SimpleThread([&](int j) {
							int stuff[5];
							for (int k = 0; k != 2048; ++k) {
								for (int x = 0; x != 5; ++x) {
									stuff[x] = (j << 16) | (k * 5 + x);
								}
								success[j] = q.enqueue_bulk(stuff, 5) && success[j];
							}
						}, i);
					}
					else {
						threads[i] = SimpleThread([&](int j) {
							for (int k = 0; k != 4096; ++k) {
								success[j] = q.enqueue((j << 16) | k) && success[j];
							}
						}, i);
					}
				}
				else {
					// Dequeue
					threads[i] = SimpleThread([&](int j) {
						int item;
						std::vector<int> prevItems(THREADS, -1);
						if (j % 4 == 1) {
							for (int k = 0; k != 2048 * 5; ++k) {
								if (!q.wait_dequeue_timed(item, 1000)) {
									--k;
									continue;
								}
								int thread = item >> 16;
								item &= 0xffff;
								if (item <= prevItems[thread]) {
									success[j] = false;
								}
								prevItems[thread] = item;
							}
						}
						else {
							int items[6];
							int k;
							for (k = 0; k < 4090; ) {
								if (std::size_t dequeued = q.wait_dequeue_bulk_timed(items, 6, 1000)) {
									for (std::size_t x = 0; x != dequeued; ++x) {
										item = items[x];
										int thread = item >> 16;
										item &= 0xffff;
										if (item <= prevItems[thread]) {
											success[j] = false;
										}
										prevItems[thread] = item;
									}
									k += (int)dequeued;
								}
							}
							for (; k != 4096; ++k) {
								if (!q.wait_dequeue_timed(item, std::chrono::hours(1))) {
									success[j] = false;
								}
								int thread = item >> 16;
								item &= 0xffff;
								if (item <= prevItems[thread]) {
									success[j] = false;
								}
								prevItems[thread] = item;
							}
						}
					}, i);
				}
			}
			for (int i = 0; i != THREADS; ++i) {
				threads[i].join();
			}
			
			for (int i = 0; i != THREADS; ++i) {
				ASSERT_OR_FAIL(success[i]);
			}
			ASSERT_OR_FAIL(q.size_approx() == 0);
			
			int item;
			ASSERT_OR_FAIL(!q.wait_dequeue_timed(item, 0));
		}
		
		// Explicit
		{
			Q q;
			ProducerToken ptok(q);
			ConsumerToken ctok(q);
			int item;
			ASSERT_OR_FAIL(!q.wait_dequeue_timed(ctok, item, 0));
			ASSERT_OR_FAIL(!q.wait_dequeue_timed(ctok, item, 1));
			ASSERT_OR_FAIL(!q.wait_dequeue_timed(ctok, item, 100));
			ASSERT_OR_FAIL(!q.wait_dequeue_timed(ctok, item, std::chrono::milliseconds(1)));
			q.enqueue(ptok, 123);
			ASSERT_OR_FAIL(q.wait_dequeue_timed(ctok, item, 0));
			ASSERT_OR_FAIL(item == 123);
		}
		
		// Explicit, threaded
		{
			Q q;
			const int THREADS = 8;
			SimpleThread threads[THREADS];
			bool success[THREADS];
			
			for (int i = 0; i != THREADS; ++i) {
				success[i] = true;
				
				if (i % 2 == 0) {
					// Enqueue
					if (i % 4 == 0) {
						threads[i] = SimpleThread([&](int j) {
							ProducerToken tok(q);
							int stuff[5];
							for (int k = 0; k != 2048; ++k) {
								for (int x = 0; x != 5; ++x) {
									stuff[x] = (j << 16) | (k * 5 + x);
								}
								success[j] = q.enqueue_bulk(tok, stuff, 5) && success[j];
							}
						}, i);
					}
					else {
						threads[i] = SimpleThread([&](int j) {
							ProducerToken tok(q);
							for (int k = 0; k != 4096; ++k) {
								success[j] = q.enqueue(tok, (j << 16) | k) && success[j];
							}
						}, i);
					}
				}
				else {
					// Dequeue
					threads[i] = SimpleThread([&](int j) {
						int item;
						std::vector<int> prevItems(THREADS, -1);
						ConsumerToken tok(q);
						if (j % 4 == 1) {
							for (int k = 0; k != 2048 * 5; ++k) {
								if (!q.wait_dequeue_timed(tok, item, 1000)) {
									--k;
									continue;
								}
								int thread = item >> 16;
								item &= 0xffff;
								if (item <= prevItems[thread]) {
									success[j] = false;
								}
								prevItems[thread] = item;
							}
						}
						else {
							int items[6];
							int k;
							for (k = 0; k < 4090; ) {
								if (std::size_t dequeued = q.wait_dequeue_bulk_timed(tok, items, 6, 1000)) {
									for (std::size_t x = 0; x != dequeued; ++x) {
										item = items[x];
										int thread = item >> 16;
										item &= 0xffff;
										if (item <= prevItems[thread]) {
											success[j] = false;
										}
										prevItems[thread] = item;
									}
									k += (int)dequeued;
								}
							}
							for (; k != 4096; ++k) {
								if (!q.wait_dequeue_timed(tok, item, std::chrono::hours(1))) {
									success[j] = false;
								}
								int thread = item >> 16;
								item &= 0xffff;
								if (item <= prevItems[thread]) {
									success[j] = false;
								}
								prevItems[thread] = item;
							}
						}
					}, i);
				}
			}
			for (int i = 0; i != THREADS; ++i) {
				threads[i].join();
			}
			
			for (int i = 0; i != THREADS; ++i) {
				ASSERT_OR_FAIL(success[i]);
			}
			ASSERT_OR_FAIL(q.size_approx() == 0);
			
			int item;
			ConsumerToken tok(q);
			ASSERT_OR_FAIL(!q.wait_dequeue_timed(tok, item, 0));
		}
		
		return true;
	}

	
	bool c_api_create()
	{
		MoodycamelCQHandle handle;
		int rc = moodycamel_cq_create(&handle);
		ASSERT_OR_FAIL(rc == 1);
		ASSERT_OR_FAIL(handle != nullptr);
		moodycamel_cq_destroy(handle);
		return true;
	}

	bool c_api_enqueue()
	{
		MoodycamelCQHandle handle;
		int rc = moodycamel_cq_create(&handle);
		int i = 10;
		rc = moodycamel_cq_enqueue(handle, &i);
		ASSERT_OR_FAIL(rc == 1);
		moodycamel_cq_destroy(handle);
		return true;
	}
	
	bool c_api_try_dequeue()
	{
		MoodycamelCQHandle handle;
		int rc = moodycamel_cq_create(&handle);
		{
			MoodycamelValue n;
			rc = moodycamel_cq_try_dequeue(handle, &n);
			ASSERT_OR_FAIL(rc == 0);
		}
		int i = 10;
		rc = moodycamel_cq_enqueue(handle, &i);
		{
			MoodycamelValue value;
			rc = moodycamel_cq_try_dequeue(handle, &value);
			int n = *reinterpret_cast<int*>(value);
			ASSERT_OR_FAIL(rc == 1);
			ASSERT_OR_FAIL(n == 10);
		}
		moodycamel_cq_destroy(handle);
		return true;
	}
	
	bool c_api_destroy()
	{
		MoodycamelCQHandle handle;
		moodycamel_cq_create(&handle);		
		moodycamel_cq_destroy(handle);
		return true;
	}

	bool acquire_and_signal()
	{
		const unsigned TIMEOUT_US = 10 * 1000 * 1000;  // 10s

		// Test resource acquisition from one other thread
		{
			LightweightSemaphore s;
			s.signal(); // Single resource available

			auto fnTestSingleAcquire = [&]() {
				for (std::size_t k = 0; k < 200000; ++k) {
					s.wait(TIMEOUT_US);
					s.signal();
				}
			};

			SimpleThread t1(fnTestSingleAcquire);
			SimpleThread t2(fnTestSingleAcquire);

			t1.join();
			t2.join();

			ASSERT_OR_FAIL(s.availableApprox() == 1);
		}

		// Test resource acquisition from multiple threads
		{
			const int THREADS = 4;
			const std::size_t ITERATIONS = 200000;
			SimpleThread threads[THREADS];
			const std::size_t arrayItemsToWait[THREADS] = { 1, 2, 3, 7 };
			LightweightSemaphore s;

			for (int i = 0; i != THREADS; ++i)
				s.signal(ITERATIONS * arrayItemsToWait[i]);

			for (int i = 0; i != THREADS; ++i) {
				threads[i] = SimpleThread([&](int tid) {
					for (std::size_t k = 0; k < ITERATIONS; ++k)
						s.waitMany(arrayItemsToWait[tid], TIMEOUT_US);
				}, i);
			}
			for (int i = 0; i != THREADS; ++i)
				threads[i].join();

			ASSERT_OR_FAIL(s.availableApprox() == 0);
		}
		{
			const int THREADS = 5;
			const std::size_t ITERATIONS = 100000;
			SimpleThread threads[THREADS];
			const std::size_t arrayItemsToWait[THREADS] = { 0, 1, 2, 3, 7 };
			LightweightSemaphore s;

			for (int i = 0; i != THREADS; ++i)
				s.signal(ITERATIONS * arrayItemsToWait[i]);

			for (int i = 0; i != THREADS; ++i) {
				threads[i] = SimpleThread([&](int tid) {
					if (tid == 0) {
						for (std::size_t k = 0; k < ITERATIONS * (THREADS - 1); ++k)
							s.wait(TIMEOUT_US);
					}
					else {
						for (std::size_t k = 0; k < ITERATIONS; ++k) {
							s.signal();
							s.waitMany(arrayItemsToWait[tid], TIMEOUT_US);
						}
					}
				}, i);
			}
			for (int i = 0; i != THREADS; ++i)
				threads[i].join();

			ASSERT_OR_FAIL(s.availableApprox() == 0);
		}

		LightweightSemaphore s;
		ASSERT_OR_FAIL(s.availableApprox() == 0);
		s.signal();
		ASSERT_OR_FAIL(s.availableApprox() == 1);
		s.signal();
		ASSERT_OR_FAIL(s.availableApprox() == 2);
		s.signal(10);
		ASSERT_OR_FAIL(s.availableApprox() == 12);
		s.signal(10);
		ASSERT_OR_FAIL(s.availableApprox() == 22);

		ASSERT_OR_FAIL(s.wait());
		ASSERT_OR_FAIL(s.availableApprox() == 21);
		ASSERT_OR_FAIL(s.wait());
		ASSERT_OR_FAIL(s.availableApprox() == 20);
		ASSERT_OR_FAIL(s.waitMany(10) == 10);
		ASSERT_OR_FAIL(s.availableApprox() == 10);
		ASSERT_OR_FAIL(s.waitMany(11) == 10);
		ASSERT_OR_FAIL(s.availableApprox() == 0);

		return true;
	}

	bool try_acquire_and_signal()
	{
		LightweightSemaphore s;

		ASSERT_OR_FAIL(s.availableApprox() == 0);

		s.signal();
		ASSERT_OR_FAIL(s.availableApprox() == 1);
		ASSERT_OR_FAIL(s.tryWaitMany(2) == 1);
		ASSERT_OR_FAIL(s.availableApprox() == 0);

		s.signal();
		ASSERT_OR_FAIL(s.availableApprox() == 1);
		ASSERT_OR_FAIL(s.tryWaitMany(3) == 1);
		ASSERT_OR_FAIL(s.availableApprox() == 0);

		s.signal(10);
		ASSERT_OR_FAIL(s.availableApprox() == 10);
		ASSERT_OR_FAIL(s.tryWaitMany(100) == 10);
		ASSERT_OR_FAIL(s.availableApprox() == 0);

		s.signal(10);
		ASSERT_OR_FAIL(s.availableApprox() == 10);
		ASSERT_OR_FAIL(s.tryWaitMany(5) == 5);
		ASSERT_OR_FAIL(s.availableApprox() == 5);

		ASSERT_OR_FAIL(s.tryWait());
		ASSERT_OR_FAIL(s.availableApprox() == 4);

		ASSERT_OR_FAIL(s.tryWait());
		ASSERT_OR_FAIL(s.availableApprox() == 3);

		return true;
	}
	
	struct TestListItem : corealgos::ListItem
	{
		int value;
		
		TestListItem()
			: value(0)
		{
			ctorCount().fetch_add(1, std::memory_order_relaxed);
		}
		
		explicit TestListItem(int value)
			: value(value)
		{
			ctorCount().fetch_add(1, std::memory_order_relaxed);
		}
		
		~TestListItem()
		{
			dtorCount().fetch_add(1, std::memory_order_relaxed);
		}
		
		inline TestListItem* prev(std::memory_order order = std::memory_order_relaxed) const
		{
			return static_cast<TestListItem*>(concurrentListPrev.load(order));
		}
		
		
		inline static void reset()
		{
			ctorCount().store(0, std::memory_order_relaxed);
			dtorCount().store(0, std::memory_order_relaxed);
		}
		
		inline static size_t constructed() { return ctorCount().load(std::memory_order_relaxed); }
		inline static size_t destructed() { return dtorCount().load(std::memory_order_relaxed); }
		
	private:
		inline static std::atomic<size_t>& ctorCount() { static std::atomic<size_t> count(0); return count; }
		inline static std::atomic<size_t>& dtorCount() { static std::atomic<size_t> count(0); return count; }
	};
	
	bool core_add_only_list()
	{
		auto destroyList = [](corealgos::ConcurrentAddOnlyList<TestListItem>& list) {
			size_t count = 0;
			
			auto tail = list.tail();
			while (tail != nullptr) {
				auto next = tail->prev();
				delete tail;
				++count;
				tail = next;
			}
			return count;
		};
		
		{
			corealgos::ConcurrentAddOnlyList<TestListItem> list;
			ASSERT_OR_FAIL(list.tail() == nullptr);
			
			ASSERT_OR_FAIL(destroyList(list) == 0);
		}
		
		{
			corealgos::ConcurrentAddOnlyList<TestListItem> list;
			for (int i = 0; i != 1000; ++i) {
				list.add(new TestListItem(i));
			}
			int i = 999;
			for (auto tail = list.tail(); tail != nullptr; tail = tail->prev()) {
				ASSERT_OR_FAIL(i == tail->value);
				--i;
			}
			ASSERT_OR_FAIL(i == -1);
			
			ASSERT_OR_FAIL(destroyList(list) == 1000);
		}
		
		for (int repeats = 0; repeats != 10; ++repeats) {
			corealgos::ConcurrentAddOnlyList<TestListItem> list;
			std::vector<SimpleThread> threads(8);
			for (size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid] = SimpleThread([&](size_t tid) {
					for (int i = 0; i != 1000; ++i) {
						list.add(new TestListItem((int)((tid << 16) | i)));
					}
				}, tid);
			}
			for (size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid].join();
			}
			
			std::vector<int> prevItems(threads.size());
			for (size_t i = 0; i != prevItems.size(); ++i) {
				prevItems[i] = 1000;
			}
			for (auto tail = list.tail(); tail != nullptr; tail = tail->prev()) {
				auto tid = tail->value >> 16;
				auto i = tail->value & ((1 << 16) - 1);
				ASSERT_OR_FAIL(prevItems[tid] == i + 1);
				prevItems[tid] = i;
			}
			
			ASSERT_OR_FAIL(destroyList(list) == 1000 * threads.size());
		}
		
		return true;
	}
	
	bool core_thread_local()
	{
		TestListItem::reset();
		{
			corealgos::ThreadLocal<TestListItem> local(4);
		}
		ASSERT_OR_FAIL(TestListItem::constructed() == 0);
		ASSERT_OR_FAIL(TestListItem::destructed() == 0);
		
		TestListItem::reset();
		{
			corealgos::ThreadLocal<TestListItem> local(4);
			local.get_or_create();
		}
		ASSERT_OR_FAIL(TestListItem::constructed() == 1);
		ASSERT_OR_FAIL(TestListItem::destructed() == 1);
		
		TestListItem::reset();
		{
			corealgos::ThreadLocal<TestListItem> local(4);
			auto item = local.get_or_create();
			item->value = 7;
			item = local.get_or_create();
			ASSERT_OR_FAIL(item->value == 7);
		}
		ASSERT_OR_FAIL(TestListItem::constructed() == 1);
		ASSERT_OR_FAIL(TestListItem::destructed() == 1);
		
		
		for (size_t initialSize = 1; initialSize <= 4; initialSize <<= 1) {
			for (int reps = 0; reps != 20; ++reps) {
				TestListItem::reset();
				{
					corealgos::ThreadLocal<TestListItem> local(initialSize);
					std::vector<SimpleThread> threads(5 * initialSize);
					std::vector<bool> failed(threads.size());
					std::atomic<std::size_t> done(0);
					for (size_t tid = 0; tid != threads.size(); ++tid) {
						threads[tid] = SimpleThread([&](size_t tid) {
							failed[tid] = false;
							auto item = local.get_or_create();
							item->value = (int)tid;
							for (int i = 0; i != 1024; ++i) {
								item = local.get_or_create();
								if (item->value != (int)tid) {
									failed[tid] = true;
								}
							}
							done.fetch_add(1, std::memory_order_seq_cst);
							while (done.load(std::memory_order_relaxed) != threads.size()) {
								moodycamel::sleep(1);
							}
						}, tid);
					}
					for (size_t tid = 0; tid != threads.size(); ++tid) {
						threads[tid].join();
						ASSERT_OR_FAIL(!failed[tid]);
					}
					ASSERT_OR_FAIL(TestListItem::constructed() == 5 * initialSize);
				}
				ASSERT_OR_FAIL(TestListItem::destructed() == 5 * initialSize);
			}
		}
		
		return true;
	}
	
	struct TestNode : corealgos::FreeListNode<TestNode>
	{
		int value;
		TestNode() { }
		explicit TestNode(int value) : value(value) { }
	};
	
	bool core_free_list()
	{
		{
			// Basic
			corealgos::FreeList<TestNode> freeList;
			ASSERT_OR_FAIL(freeList.try_get() == nullptr);
			
			freeList.add(new TestNode(7));
			TestNode* node = freeList.try_get();
			ASSERT_OR_FAIL(node != nullptr);
			ASSERT_OR_FAIL(node->value == 7);
			ASSERT_OR_FAIL(freeList.try_get() == nullptr);
			
			freeList.add(node);
			node = freeList.try_get();
			ASSERT_OR_FAIL(node != nullptr);
			ASSERT_OR_FAIL(node->value == 7);
			ASSERT_OR_FAIL(freeList.try_get() == nullptr);
			delete node;
		}
		
		{
			// Multi-threaded. Tests ABA too.
			for (int rep = 0; rep != 10; ++rep) {
				corealgos::FreeList<TestNode> freeList;
				std::vector<SimpleThread> threads(rep < 8 ? 4 : 16);
				std::vector<bool> failed(threads.size());
				std::vector<TestNode> initialNodes(threads.size());
				const int OP_COUNT = 2048;
				for (size_t tid = 0; tid != threads.size(); ++tid) {
					threads[tid] = SimpleThread([&](size_t tid) {
						std::vector<bool> seenValues(threads.size() * OP_COUNT, false);
						failed[tid] = false;
						TestNode* node = &initialNodes[tid];
						node->value = ((int)tid << 20) | 1;
						freeList.add(node);
						for (int i = 1; i != OP_COUNT - 1; ++i) {
							node = freeList.try_get();
							if (node != nullptr) {
								auto seen = seenValues.begin() + ((node->value >> 20) * OP_COUNT + (node->value & 0xFFFFF));
								if (*seen) {
									failed[tid] = true;
								}
								*seen = true;
								
								node->value = ((int)tid << 20) | (i + 1);
								freeList.add(node);
							}
						}
					}, tid);
				}
				for (size_t tid = 0; tid != threads.size(); ++tid) {
					threads[tid].join();
					ASSERT_OR_FAIL(!failed[tid]);
				}
				for (size_t tid = 0; tid != threads.size(); ++tid) {
					auto node = freeList.try_get();
					ASSERT_OR_FAIL(node != nullptr);
					ASSERT_OR_FAIL(node->value != -1);
					node->value = -1;
				}
				auto node = freeList.try_get();
				ASSERT_OR_FAIL(node == nullptr);
			}
		}
		
		return true;
	}
	
	bool core_spmc_hash()
	{
		{
			for (int rep = 0; rep != 20; ++rep) {
				corealgos::SPMCSequentialHashMap<int> hash(rep < 10 ? 2 : 4);
				std::vector<SimpleThread> threads(rep < 12 ? 4 : 16);
				std::vector<bool> failed(threads.size());
				
				const int MAX_ENTRIES = 4096;
				std::vector<int> values(MAX_ENTRIES);
				std::array<std::atomic<int>, MAX_ENTRIES> useCounts;
				std::array<std::atomic<bool>, MAX_ENTRIES> removed;
				
				for (std::size_t i = 0; i != useCounts.size(); ++i) {
					useCounts[i].store(0, std::memory_order_relaxed);
					removed[i].store(false, std::memory_order_relaxed);
				}
				
				for (size_t tid = 0; tid != threads.size(); ++tid) {
					threads[tid] = SimpleThread([&](size_t tid) {
						failed[tid] = false;
						
						if (tid == 0) {
							// Producer thread
							for (int i = 0; i != MAX_ENTRIES; ++i) {
								values[i] = i;
								hash.insert(i, &values[i]);
								useCounts[i].store((int)threads.size() / 2, std::memory_order_release);
							}
						}
						else {
							// One of the consumer threads
							for (int i = MAX_ENTRIES * 2; i != 0; --i) {	// Purposefully off-by-lots
								int useCount = -1;
								if (i < MAX_ENTRIES) {
									useCount = useCounts[i].fetch_add(-1, std::memory_order_acquire);
								}
								
								int* val;
								if (useCount > 0) {
									val = hash.find(i);
									bool isRemoved = removed[i].load(std::memory_order_relaxed);
									assert(val == nullptr || *val == *val);		// Find segfaults
									
									// We read the use count again; if it's still > 0, the item must have been in
									// the hash during the entire call to find(), so we can check its value
									auto currentUseCount = useCounts[i].fetch_add(0, std::memory_order_release);
									if ((currentUseCount > 0 || (currentUseCount == 0 && useCount == 1)) && (val == nullptr || *val != i || isRemoved)) {
										failed[tid] = true;
									}
								}
								if (useCount == 1) {
									val = hash.remove(i);
									if (val == nullptr || *val != i || removed[i].load(std::memory_order_relaxed)) {
										failed[tid] = true;
									}
									removed[i].store(true, std::memory_order_release);
								}
							}
						}
					}, tid);
				}
				for (size_t tid = 0; tid != threads.size(); ++tid) {
					threads[tid].join();
					ASSERT_OR_FAIL(!failed[tid]);
				}
				for (int i = 0; i != MAX_ENTRIES; ++i) {
					auto val = hash.find(i);
					if (val != nullptr) {
						ASSERT_OR_FAIL(&values[i] == val && *val == i && !removed[i].load(std::memory_order_relaxed));
					}
					else {
						ASSERT_OR_FAIL(removed[i].load(std::memory_order_relaxed));
					}
					auto removedVal = hash.remove(i);
					ASSERT_OR_FAIL(removedVal == val);
				}
				for (int i = 0; i != MAX_ENTRIES; ++i) {
					ASSERT_OR_FAIL(hash.find(i) == nullptr);
					ASSERT_OR_FAIL(hash.remove(i) == nullptr);
				}
				ASSERT_OR_FAIL(hash.find(MAX_ENTRIES) == nullptr);
				ASSERT_OR_FAIL(hash.remove(MAX_ENTRIES) == nullptr);
			}
		}
		return true;
	}
	
	bool explicit_strings_threaded()
	{
		std::vector<SimpleThread> threads(8);
		ConcurrentQueue<std::string, MallocTrackingTraits> q(1024 * 1024);
		
		for (size_t tid = 0; tid != threads.size(); ++tid) {
			threads[tid] = SimpleThread([&](size_t tid) {
				const size_t ITERATIONS = 100 * 1024;
				if (tid % 2 == 0) {
					// Produce
					ProducerToken t(q);
					for (size_t i = 0; i != ITERATIONS; ++i) {
						q.enqueue(t, std::string("banana", i % 6));
					}
				}
				else {
					// Consume
					std::string item;
					for (size_t i = 0; i != ITERATIONS / 2; ++i) {
						q.try_dequeue(item);
					}
				}
			}, tid);
		}
		for (size_t tid = 0; tid != threads.size(); ++tid) {
			threads[tid].join();
		}
		
		return true;
	}

	bool large_traits()
	{
		union Elem { uint32_t x; char dummy[156]; };

		ConcurrentQueue<Elem, LargeTraits> q(10000, 0, 48);
		std::vector<SimpleThread> threads(48);
		for (size_t tid = 0; tid != threads.size(); ++tid) {
			threads[tid] = SimpleThread([&](size_t tid) {
				const size_t ELEMENTS = 5000;
				if (tid != 0) {
					// Produce
					for (uint32_t i = 0; i != ELEMENTS; ++i)
						q.try_enqueue(Elem { ((uint32_t)tid << 16) | i });
				}
				else {
					// Consume
					Elem item[256];
					for (size_t i = 0; i != ELEMENTS * 200; ++i)
						q.try_dequeue_bulk(item, sizeof(item) / sizeof(item[0]));
				}
			}, tid);
		}
		for (size_t tid = 0; tid != threads.size(); ++tid) {
			threads[tid].join();
		}

		return true;
	}
};

}


void printTests(ConcurrentQueueTests const& tests)
{
	std::printf("   Supported tests are:\n");
	
	std::vector<std::string> names;
	tests.getAllTestNames(names);
	for (auto it = names.cbegin(); it != names.cend(); ++it) {
		std::printf("      %s\n", it->c_str());
	}
}


// Basic test harness
#if !defined(TARGET_OS_IPHONE)
int main(int argc, char** argv)
{
	bool disablePrompt = false;
	unsigned int iterations = 8;
	std::vector<std::string> selectedTests;
	
	// Disable buffering (so that when run in, e.g., Sublime Text, the output appears as it is written)
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	
	// Isolate the executable name
	std::string progName = argv[0];
	auto slash = progName.find_last_of("/\\");
	if (slash != std::string::npos) {
		progName = progName.substr(slash + 1);
	}
	
	ConcurrentQueueTests tests;
	
	// Parse command line options
	if (argc > 1) {
		bool printHelp = false;
		bool printedTests = false;
		bool error = false;
		for (int i = 1; i < argc; ++i) {
			if (std::strcmp(argv[i], "--help") == 0) {
				printHelp = true;
			}
			else if (std::strcmp(argv[i], "--disable-prompt") == 0) {
				disablePrompt = true;
			}
			else if (std::strcmp(argv[i], "--run") == 0) {
				if (i + 1 == argc || argv[i + 1][0] == '-') {
					std::printf("Expected test name argument for --run option.\n");
					if (!printedTests) {
						printTests(tests);
						printedTests = true;
					}
					error = true;
					continue;
				}
				
				if (!tests.validateTestName(argv[++i])) {
					std::printf("Unrecognized test '%s'.\n", argv[i]);
					if (!printedTests) {
						printTests(tests);
						printedTests = true;
					}
					error = true;
					continue;
				}
				
				selectedTests.push_back(argv[i]);
			}
			else if (std::strcmp(argv[i], "--iterations") == 0) {
				if (i + 1 == argc || argv[i + 1][0] == '-') {
					std::printf("Expected iteration count argument for --iterations option.\n");
					error = true;
					continue;
				}
				
				iterations = static_cast<unsigned int>(std::atoi(argv[++i]));
			}
			else {
				std::printf("Unrecognized option '%s'.\n", argv[i]);
				error = true;
			}
		}
		
		if (error || printHelp) {
			if (error) {
				std::printf("\n");
			}
			std::printf("%s\n    Description: Runs unit tests for moodycamel::ConcurrentQueue\n", progName.c_str());
			std::printf("    --help            Prints this help blurb\n");
			std::printf("    --run test        Runs only the specified test(s)\n");
			std::printf("    --iterations N    Do N iterations of each test\n");
			std::printf("    --disable-prompt  Disables prompt before exit when the tests finish\n");
			return error ? -1 : 0;
		}
	}
	
	int exitCode = 0;
	
	bool result;
	if (selectedTests.size() > 0) {
		std::printf("Running %d iteration%s of selected unit test%s for moodycamel::ConcurrentQueue.\n\n", iterations, iterations == 1 ? "" : "s", selectedTests.size() == 1 ? "" : "s");
		result = tests.run(selectedTests, iterations);
	}
	else {
		std::printf("Running %d iteration%s of all unit tests for moodycamel::ConcurrentQueue.\n(Run %s --help for other options.)\n\n", iterations, iterations == 1 ? "" : "s", progName.c_str());
		result = tests.run(iterations);
	}
	
	if (result) {
		std::printf("All %stests passed.\n", (selectedTests.size() > 0 ? "selected " : ""));
	}
	else {
		std::printf("Test(s) failed!\n");
		exitCode = 2;
	}
	
	if (!disablePrompt) {
		std::printf("Press ENTER to exit.\n");
		getchar();
	}
	return exitCode;
}
#else
// Provide entry function that can be invoked
// by a test host (iOS app / test runner)
bool runAllTests() {
  unsigned int iterations = 8;
  ConcurrentQueueTests tests;
  return tests.run(iterations);
}
#endif // !defined(TARGET_OS_IPHONE)
