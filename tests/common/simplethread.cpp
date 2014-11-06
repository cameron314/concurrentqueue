// ©2013 Cameron Desrochers

#include "simplethread.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct SimpleThread::ThreadRef
{
	HANDLE handle;

	static DWORD WINAPI ThreadProc(LPVOID param)
	{
		auto threadRef = static_cast<ThreadRef*>(param);
		threadRef->callbackFunc(threadRef->callbackObj);
		return 0;
	}
	
	ThreadRef(void* callbackObj, CallbackFunc callbackFunc)
		: callbackObj(callbackObj), callbackFunc(callbackFunc)
	{
	}
	
	void* callbackObj;
	CallbackFunc callbackFunc;
};

void SimpleThread::startThread(void* callbackObj, CallbackFunc callbackFunc)
{
	thread = new ThreadRef(callbackObj, callbackFunc);
	thread->handle = CreateThread(NULL, StackSize, &ThreadRef::ThreadProc, thread, 0, NULL);
}

void SimpleThread::join()
{
	if (thread != nullptr && thread->handle != NULL) {
		WaitForSingleObject(thread->handle, INFINITE);
		CloseHandle(thread->handle);
		thread->handle = NULL;
	}
}
#else
#include <thread>

struct SimpleThread::ThreadRef
{
	std::thread thread;

	static void threadProc(ThreadRef* threadRef)
	{
		threadRef->callbackFunc(threadRef->callbackObj);
	}
	
	ThreadRef(void* callbackObj, CallbackFunc callbackFunc)
		: callbackObj(callbackObj), callbackFunc(callbackFunc)
	{
	}
	
	void* callbackObj;
	CallbackFunc callbackFunc;
};

void SimpleThread::startThread(void* callbackObj, CallbackFunc callbackFunc)
{
	thread = new ThreadRef(callbackObj, callbackFunc);
	thread->thread = std::thread(&ThreadRef::threadProc, thread);
}

void SimpleThread::join()
{
	if (thread != nullptr && thread->thread.joinable()) {
		thread->thread.join();
	}
}
#endif

SimpleThread::~SimpleThread()
{
	if (thread != nullptr) {
		join();
		delete thread;
	}
}
