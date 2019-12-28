#pragma once

// Use relacy assertions
#undef assert
#ifdef NDEBUG
#define assert(x)
#else
#define assert(x) RL_ASSERT(x)
#endif


struct RelacyThreadExitListener
{
	typedef void (*callback_t)(void*);
	callback_t callback;
	void* userData;
	
	RelacyThreadExitListener* next;
};

class RelacyThreadExitNotifier
{
public:
	static void subscribe(RelacyThreadExitListener* listener)
	{
		auto& tlsInst = instance();
		listener->next = tlsInst.tail;
		tlsInst.tail = listener;
	}
	
	static void unsubscribe(RelacyThreadExitListener* listener)
	{
		auto& tlsInst = instance();
		RelacyThreadExitListener** prev = &tlsInst.tail;
		for (auto ptr = tlsInst.tail; ptr != nullptr; ptr = ptr->next) {
			if (ptr == listener) {
				*prev = ptr->next;
				break;
			}
			prev = &ptr->next;
		}
	}
	
	static void notify_relacy_thread_start()
	{
		instance().tail = nullptr;
	}
	
	static void notify_relacy_thread_exit()
	{
		for (auto ptr = instance().tail; ptr != nullptr; ptr = ptr->next) {
			ptr->callback(ptr->userData);
		}
	}
	
	
private:
	RelacyThreadExitNotifier() : tail(nullptr) { }
	
	static RelacyThreadExitNotifier& instance()
	{
		static RelacyThreadExitNotifier instances[1024];
		
		auto tid = rl::thread_index();
		assert(tid < 1024);
		return instances[tid];
	}
	
private:
	RelacyThreadExitListener* tail;
};

namespace std
{
	// Relacy doesn't wrap std::atomic_flag
	struct atomic_flag {
	private:
		atomic_flag(atomic_flag const&);
		atomic_flag(atomic_flag&&);
		atomic_flag& operator=(atomic_flag const&);
		atomic_flag& operator=(atomic_flag&&);
	
	public:
		atomic_flag() { }
		atomic_flag(bool initialValue) : val(initialValue ? 1 : 0) { }
		
		void clear()
		{
			clear(std::memory_order_seq_cst);
		}
		
		void clear(rl::memory_order order, rl::debug_info_param d)
		{
			val.store(0, order, d);
		}
		
		bool test_and_set()
		{
			return test_and_set(std::memory_order_seq_cst);
		}
		
		bool test_and_set(rl::memory_order order, rl::debug_info_param d)
		{
			return val.fetch_or(1, order, d) != 0;
		}
		
	private:
		std::atomic<int> val;
	};
}
