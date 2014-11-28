// ©2013 Cameron Desrochers

#pragma once

// Like C++11's std::thread, but with a reduced API, and works on Windows with MSVC2010+.
// Wraps std::thread on other OSes. Perhaps the most significant departure between
// std::thread and this mini-library is that join() is called implicitly in the destructor,
// if the thread is joinable. The thread callback functions should not throw exceptions.

#include <utility>
#include <type_traits>


namespace details
{
	template<typename TArg1 = void, typename TArg2 = void, typename TArg3 = void>
	struct ArgWrapper
	{
		typename std::remove_reference<TArg1>::type arg1;
		typename std::remove_reference<TArg2>::type arg2;
		typename std::remove_reference<TArg3>::type arg3;
		ArgWrapper(ArgWrapper const& o) : arg1(o.arg1), arg2(o.arg2), arg3(o.arg3) { }
		ArgWrapper(ArgWrapper&& o) : arg1(std::move(o.arg1)), arg2(std::move(o.arg2)), arg3(std::move(o.arg3)) { }
		template<typename T, typename U, typename V>
		ArgWrapper(T&& a1, U&& a2, V&& a3) : arg1(std::forward<T>(a1)), arg2(std::forward<U>(a2)), arg3(std::forward<V>(a3)) { }
		template<typename TCallback>
		void callCallback(TCallback&& callback) const { std::forward<TCallback>(callback)(std::move(arg1), std::move(arg2), std::move(arg3)); }
	};
	
	template<typename TArg1, typename TArg2>
	struct ArgWrapper<TArg1, TArg2, void>
	{
		typename std::remove_reference<TArg1>::type arg1;
		typename std::remove_reference<TArg2>::type arg2;
		ArgWrapper(ArgWrapper const& o) : arg1(o.arg1), arg2(o.arg2) { }
		ArgWrapper(ArgWrapper&& o) : arg1(std::move(o.arg1)), arg2(std::move(o.arg2)) { }
		template<typename T, typename U>
		ArgWrapper(T&& a1, U&& a2) : arg1(std::forward<T>(a1)), arg2(std::forward<U>(a2)) { }
		template<typename TCallback>
		void callCallback(TCallback&& callback) const { std::forward<TCallback>(callback)(std::move(arg1), std::move(arg2)); }
	};
	
	template<typename TArg1>
	struct ArgWrapper<TArg1, void, void>
	{
		typename std::remove_reference<TArg1>::type arg1;
		ArgWrapper(ArgWrapper const& o) : arg1(o.arg1) { }
		ArgWrapper(ArgWrapper&& o) : arg1(std::move(o.arg1)) { }
		template<typename T>
		ArgWrapper(T&& a1) : arg1(std::forward<T>(a1)) { }
		template<typename TCallback>
		void callCallback(TCallback&& callback) const { std::forward<TCallback>(callback)(std::move(arg1)); }
	};
	
	template<> struct ArgWrapper<void, void, void>
	{
		template<typename TCallback> void callCallback(TCallback&& callback) const { std::forward<TCallback>(callback)(); }
	};
}


class SimpleThread
{
private:
	struct ThreadRef;
	
	template<typename TCallback, typename TArgs>
	struct CallbackWrapper
	{
		template<typename U>
		CallbackWrapper(TCallback&& callback, U&& args)
			: callback(std::forward<TCallback>(callback)), args(std::forward<U>(args))
		{
		}

		static void callAndDelete(void* wrapper)
		{
			auto typedWrapper = static_cast<CallbackWrapper*>(wrapper);
			typedWrapper->args.callCallback(std::move(typedWrapper->callback));
			delete typedWrapper;
		}

		typename std::decay<TCallback>::type callback;
		TArgs args;
	};
	
	typedef void (*CallbackFunc)(void*);

	void startThread(void* callbackObj, CallbackFunc callbackFunc);


public:
	static const int StackSize = 4 * 1024;	// bytes

	SimpleThread() : thread(nullptr) {  }

	SimpleThread(SimpleThread&& other)
		: thread(other.thread)
	{
		other.thread = nullptr;
	}
	
	SimpleThread& operator=(SimpleThread&& other)
	{
		thread = other.thread;
		other.thread = nullptr;
		return *this;
	}
	
	// Disable copying and copy-assignment
private:
	SimpleThread(SimpleThread const&);
	SimpleThread& operator=(SimpleThread const&);
public:

	template<typename TCallback>
	explicit SimpleThread(TCallback&& callback)
	{
		auto wrapper = new CallbackWrapper<TCallback, ::details::ArgWrapper<>>(
			std::forward<TCallback>(callback),
			::details::ArgWrapper<>()
		);
		startThread(wrapper, &CallbackWrapper<TCallback, ::details::ArgWrapper<>>::callAndDelete);
	}

	template<typename TCallback, typename TArg1>
	explicit SimpleThread(TCallback&& callback, TArg1&& arg1)
	{
		auto wrapper = new CallbackWrapper<TCallback, ::details::ArgWrapper<TArg1>>(
			std::forward<TCallback>(callback),
			::details::ArgWrapper<TArg1>(std::forward<TArg1>(arg1))
		);
		startThread(wrapper, &CallbackWrapper<TCallback, ::details::ArgWrapper<TArg1>>::callAndDelete);
	}

	template<typename TCallback, typename TArg1, typename TArg2>
	explicit SimpleThread(TCallback&& callback, TArg1&& arg1, TArg2&& arg2)
	{
		auto wrapper = new CallbackWrapper<TCallback, ::details::ArgWrapper<TArg1, TArg2>>(
			std::forward<TCallback>(callback),
			::details::ArgWrapper<TArg1, TArg2>(std::forward<TArg1>(arg1), std::forward<TArg2>(arg2))
		);
		startThread(wrapper, &CallbackWrapper<TCallback, ::details::ArgWrapper<TArg1, TArg2>>::callAndDelete);
	}

	template<typename TCallback, typename TArg1, typename TArg2, typename TArg3>
	explicit SimpleThread(TCallback&& callback, TArg1&& arg1, TArg2&& arg2, TArg3&& arg3)
	{
		auto wrapper = new CallbackWrapper<TCallback, ::details::ArgWrapper<TArg1, TArg2, TArg3>>(
			std::forward<TCallback>(callback),
			::details::ArgWrapper<TArg1, TArg2, TArg3>(std::forward<TArg1>(arg1), std::forward<TArg2>(arg2), std::forward<TArg3>(arg3))
		);
		startThread(wrapper, &CallbackWrapper<TCallback, ::details::ArgWrapper<TArg1, TArg2, TArg3>>::callAndDelete);
	}
	
	~SimpleThread();

	void join();

private:
	ThreadRef* thread;
};
