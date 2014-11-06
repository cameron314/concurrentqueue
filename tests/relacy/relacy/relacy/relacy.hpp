/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_RELACY_HPP
#define RL_RELACY_HPP
#ifdef _MSC_VER
#   pragma once
#endif


#include "base.hpp"
#include "context.hpp"
#include "context_base_impl.hpp"
#include "backoff.hpp"
#include "atomic_fence.hpp"
#include "atomic.hpp"
#include "var.hpp"
#include "thread_local.hpp"
#include "test_suite.hpp"
#include "dyn_thread.hpp"

#include "stdlib/mutex.hpp"
#include "stdlib/condition_variable.hpp"
#include "stdlib/semaphore.hpp"
#include "stdlib/event.hpp"

#include "stdlib/windows.hpp"
#include "stdlib/pthread.hpp"

#define VAR_T(x) rl::var<x>
#define TLS_T(T) rl::thread_local_var<T>
#define VAR(x) x($)

#ifndef RL_FORCE_SEQ_CST
#define memory_order_relaxed mo_relaxed, $
#define memory_order_consume mo_consume, $
#define memory_order_acquire mo_acquire, $
#define memory_order_release mo_release, $
#define memory_order_acq_rel mo_acq_rel, $
#define memory_order_seq_cst mo_seq_cst, $
#else
#define memory_order_relaxed mo_seq_cst, $
#define memory_order_consume mo_seq_cst, $
#define memory_order_acquire mo_seq_cst, $
#define memory_order_release mo_seq_cst, $
#define memory_order_acq_rel mo_seq_cst, $
#define memory_order_seq_cst mo_seq_cst, $
#endif

#define new                 RL_NEW_PROXY
#define delete              RL_DELETE_PROXY
#define malloc(sz)          rl::rl_malloc((sz), $)
#define calloc(sz, cnt)     rl::rl_calloc((sz), (cnt), $)
#define realloc(p, sz)      rl::rl_realloc((p), (sz), $)
#define free(p)             rl::rl_free((p), $)

#ifdef assert
#undef assert
#endif
#define assert              RL_ASSERT

#ifdef errno
#undef errno
#endif
#define errno               (rl::get_errno())

#endif
