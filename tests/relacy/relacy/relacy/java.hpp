/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_JAVA_HPP
#define RL_JAVA_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"


namespace rl
{

/*

Hierarchy For Package java.util.concurrent.locks 

Class Hierarchy

    * java.lang.Object
          o java.util.concurrent.locks.AbstractQueuedSynchronizer (implements java.io.Serializable)
          o java.util.concurrent.locks.AbstractQueuedSynchronizer.ConditionObject (implements java.util.concurrent.locks.Condition, java.io.Serializable)
          o java.util.concurrent.locks.LockSupport
          o java.util.concurrent.locks.ReentrantLock (implements java.util.concurrent.locks.Lock, java.io.Serializable)
          o java.util.concurrent.locks.ReentrantReadWriteLock (implements java.util.concurrent.locks.ReadWriteLock, java.io.Serializable)
          o java.util.concurrent.locks.ReentrantReadWriteLock.ReadLock (implements java.util.concurrent.locks.Lock, java.io.Serializable)
          o java.util.concurrent.locks.ReentrantReadWriteLock.WriteLock (implements java.util.concurrent.locks.Lock, java.io.Serializable) 

Interface Hierarchy

    * java.util.concurrent.locks.Condition
    * java.util.concurrent.locks.Lock
    * java.util.concurrent.locks.ReadWriteLock
*/





/*

java.util.concurrent.Semaphore



Public Constructors
public Semaphore(int permits)
Creates a Semaphore with the given number of permits and nonfair fairness setting.
Parameters
permits     the initial number of permits available. This value may be negative, in which case releases must occur before any acquires will be granted.
public Semaphore(int permits, boolean fair)
Creates a Semaphore with the given number of permits and the given fairness setting.
Parameters
permits     the initial number of permits available. This value may be negative, in which case releases must occur before any acquires will be granted.
fair     true if this semaphore will guarantee first-in first-out granting of permits under contention, else false.
Public Methods
public void acquire()
Acquires a permit from this semaphore, blocking until one is available, or the thread is interrupted.

Acquires a permit, if one is available and returns immediately, reducing the number of available permits by one.

If no permit is available then the current thread becomes disabled for thread scheduling purposes and lies dormant until one of two things happens:

    * Some other thread invokes the release() method for this semaphore and the current thread is next to be assigned a permit; or
    * Some other thread interrupts the current thread. 

If the current thread:

    * has its interrupted status set on entry to this method; or
    * is interrupted while waiting for a permit, 

then InterruptedException is thrown and the current thread's interrupted status is cleared.
Throws
InterruptedException     if the current thread is interrupted
See Also

    * interrupt()

public void acquire(int permits)
Acquires the given number of permits from this semaphore, blocking until all are available, or the thread is interrupted.

Acquires the given number of permits, if they are available, and returns immediately, reducing the number of available permits by the given amount.

If insufficient permits are available then the current thread becomes disabled for thread scheduling purposes and lies dormant until one of two things happens:

    * Some other thread invokes one of the release methods for this semaphore, the current thread is next to be assigned permits and the number of available permits satisfies this request; or
    * Some other thread interrupts the current thread. 

If the current thread:

    * has its interrupted status set on entry to this method; or
    * is interrupted while waiting for a permit, 

then InterruptedException is thrown and the current thread's interrupted status is cleared. Any permits that were to be assigned to this thread are instead assigned to the next waiting thread(s), as if they had been made available by a call to release().
Parameters
permits     the number of permits to acquire
Throws
InterruptedException     if the current thread is interrupted
IllegalArgumentException     if permits less than zero.
See Also

    * interrupt()

public void acquireUninterruptibly(int permits)
Acquires the given number of permits from this semaphore, blocking until all are available.

Acquires the given number of permits, if they are available, and returns immediately, reducing the number of available permits by the given amount.

If insufficient permits are available then the current thread becomes disabled for thread scheduling purposes and lies dormant until some other thread invokes one of the release methods for this semaphore, the current thread is next to be assigned permits and the number of available permits satisfies this request.

If the current thread is interrupted while waiting for permits then it will continue to wait and its position in the queue is not affected. When the thread does return from this method its interrupt status will be set.
Parameters
permits     the number of permits to acquire
Throws
IllegalArgumentException     if permits less than zero.
public void acquireUninterruptibly()
Acquires a permit from this semaphore, blocking until one is available.

Acquires a permit, if one is available and returns immediately, reducing the number of available permits by one.

If no permit is available then the current thread becomes disabled for thread scheduling purposes and lies dormant until some other thread invokes the release() method for this semaphore and the current thread is next to be assigned a permit.

If the current thread is interrupted while waiting for a permit then it will continue to wait, but the time at which the thread is assigned a permit may change compared to the time it would have received the permit had no interruption occurred. When the thread does return from this method its interrupt status will be set.
public int availablePermits()
Returns the current number of permits available in this semaphore.

This method is typically used for debugging and testing purposes.
Returns

    * the number of permits available in this semaphore. 

public int drainPermits()
Acquire and return all permits that are immediately available.
Returns

    * the number of permits 

public final int getQueueLength()
Returns an estimate of the number of threads waiting to acquire. The value is only an estimate because the number of threads may change dynamically while this method traverses internal data structures. This method is designed for use in monitoring of the system state, not for synchronization control.
Returns

    * the estimated number of threads waiting for this lock 

public final boolean hasQueuedThreads()
Queries whether any threads are waiting to acquire. Note that because cancellations may occur at any time, a true return does not guarantee that any other thread will ever acquire. This method is designed primarily for use in monitoring of the system state.
Returns

    * true if there may be other threads waiting to acquire the lock. 

public boolean isFair()
Returns true if this semaphore has fairness set true.
Returns

    * true if this semaphore has fairness set true. 

public void release(int permits)
Releases the given number of permits, returning them to the semaphore.

Releases the given number of permits, increasing the number of available permits by that amount. If any threads are blocking trying to acquire permits, then the one that has been waiting the longest is selected and given the permits that were just released. If the number of available permits satisfies that thread's request then that thread is re-enabled for thread scheduling purposes; otherwise the thread continues to wait. If there are still permits available after the first thread's request has been satisfied, then those permits are assigned to the next waiting thread. If it is satisfied then it is re-enabled for thread scheduling purposes. This continues until there are insufficient permits to satisfy the next waiting thread, or there are no more waiting threads.

There is no requirement that a thread that releases a permit must have acquired that permit by calling acquire. Correct usage of a semaphore is established by programming convention in the application.
Parameters
permits     the number of permits to release
Throws
IllegalArgumentException     if permits less than zero.
public void release()
Releases a permit, returning it to the semaphore.

Releases a permit, increasing the number of available permits by one. If any threads are blocking trying to acquire a permit, then one is selected and given the permit that was just released. That thread is re-enabled for thread scheduling purposes.

There is no requirement that a thread that releases a permit must have acquired that permit by calling acquire(). Correct usage of a semaphore is established by programming convention in the application.
public String toString()
Returns a string identifying this semaphore, as well as its state. The state, in brackets, includes the String "Permits =" followed by the number of permits.
Returns

    * a string identifying this semaphore, as well as its state 

public boolean tryAcquire(long timeout, TimeUnit unit)
Acquires a permit from this semaphore, if one becomes available within the given waiting time and the current thread has not been interrupted.

Acquires a permit, if one is available and returns immediately, with the value true, reducing the number of available permits by one.

If no permit is available then the current thread becomes disabled for thread scheduling purposes and lies dormant until one of three things happens:

    * Some other thread invokes the release() method for this semaphore and the current thread is next to be assigned a permit; or
    * Some other thread interrupts the current thread; or
    * The specified waiting time elapses. 

If a permit is acquired then the value true is returned.

If the current thread:

    * has its interrupted status set on entry to this method; or
    * is interrupted while waiting to acquire a permit, 

then InterruptedException is thrown and the current thread's interrupted status is cleared.

If the specified waiting time elapses then the value false is returned. If the time is less than or equal to zero, the method will not wait at all.
Parameters
timeout     the maximum time to wait for a permit
unit     the time unit of the timeout argument.
Returns

    * true if a permit was acquired and false if the waiting time elapsed before a permit was acquired.

Throws
InterruptedException     if the current thread is interrupted
See Also

    * interrupt()

public boolean tryAcquire(int permits, long timeout, TimeUnit unit)
Acquires the given number of permits from this semaphore, if all become available within the given waiting time and the current thread has not been interrupted.

Acquires the given number of permits, if they are available and returns immediately, with the value true, reducing the number of available permits by the given amount.

If insufficient permits are available then the current thread becomes disabled for thread scheduling purposes and lies dormant until one of three things happens:

    * Some other thread invokes one of the release methods for this semaphore, the current thread is next to be assigned permits and the number of available permits satisfies this request; or
    * Some other thread interrupts the current thread; or
    * The specified waiting time elapses. 

If the permits are acquired then the value true is returned.

If the current thread:

    * has its interrupted status set on entry to this method; or
    * is interrupted while waiting to acquire the permits, 

then InterruptedException is thrown and the current thread's interrupted status is cleared. Any permits that were to be assigned to this thread, are instead assigned to the next waiting thread(s), as if they had been made available by a call to release().

If the specified waiting time elapses then the value false is returned. If the time is less than or equal to zero, the method will not wait at all. Any permits that were to be assigned to this thread, are instead assigned to the next waiting thread(s), as if they had been made available by a call to release().
Parameters
permits     the number of permits to acquire
timeout     the maximum time to wait for the permits
unit     the time unit of the timeout argument.
Returns

    * true if all permits were acquired and false if the waiting time elapsed before all permits were acquired.

Throws
InterruptedException     if the current thread is interrupted
IllegalArgumentException     if permits less than zero.
See Also

    * interrupt()

public boolean tryAcquire(int permits)
Acquires the given number of permits from this semaphore, only if all are available at the time of invocation.

Acquires the given number of permits, if they are available, and returns immediately, with the value true, reducing the number of available permits by the given amount.

If insufficient permits are available then this method will return immediately with the value false and the number of available permits is unchanged.

Even when this semaphore has been set to use a fair ordering policy, a call to tryAcquire will immediately acquire a permit if one is available, whether or not other threads are currently waiting. This "barging" behavior can be useful in certain circumstances, even though it breaks fairness. If you want to honor the fairness setting, then use tryAcquire(permits, 0, TimeUnit.SECONDS) which is almost equivalent (it also detects interruption).
Parameters
permits     the number of permits to acquire
Returns

    * true if the permits were acquired and false otherwise.

Throws
IllegalArgumentException     if permits less than zero.
public boolean tryAcquire()
Acquires a permit from this semaphore, only if one is available at the time of invocation.

Acquires a permit, if one is available and returns immediately, with the value true, reducing the number of available permits by one.

If no permit is available then this method will return immediately with the value false.

Even when this semaphore has been set to use a fair ordering policy, a call to tryAcquire() will immediately acquire a permit if one is available, whether or not other threads are currently waiting. This "barging" behavior can be useful in certain circumstances, even though it breaks fairness. If you want to honor the fairness setting, then use tryAcquire(0, TimeUnit.SECONDS) which is almost equivalent (it also detects interruption).
Returns

    * true if a permit was acquired and false otherwise. 

Protected Methods
protected Collection<Thread> getQueuedThreads()
Returns a collection containing threads that may be waiting to acquire. Because the actual set of threads may change dynamically while constructing this result, the returned collection is only a best-effort estimate. The elements of the returned collection are in no particular order. This method is designed to facilitate construction of subclasses that provide more extensive monitoring facilities.
Returns

    * the collection of threads 

protected void reducePermits(int reduction)
Shrinks the number of available permits by the indicated reduction. This method can be useful in subclasses that use semaphores to track resources that become unavailable. This method differs from acquire in that it does not block waiting for permits to become available.
Parameters
reduction     the number of permits to remove
Throws
IllegalArgumentException     if reduction is negative 
*/

}

#endif
