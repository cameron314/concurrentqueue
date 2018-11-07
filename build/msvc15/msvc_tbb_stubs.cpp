#include "tbb/tbb_main.h"
#include "tbb/itt_notify.h"

#include <atomic>

namespace tbb {
	namespace internal {
		// Taken from tbb_main.cpp (which doesn't compile as-is)

		void call_itt_notify_v5(int /*t*/, void* /*ptr*/) {}

		void itt_store_pointer_with_release_v3(void* dst, void* src) {
			ITT_NOTIFY(sync_releasing, dst);
			__TBB_store_with_release(*static_cast<void**>(dst), src);
		}
	}
}

extern "C" {
#ifdef _WIN64
	__int8 __TBB_machine_cmpswp1(volatile void *ptr, __int8 value, __int8 comparand) {
		return _InterlockedCompareExchange8((__int8*)ptr, value, comparand);
	}
#else
	// Taken from tbb/machine/windows_intel64.h, which isn't included in 32-bit mode but is
	// still used for some reason
	__int64 __TBB_machine_cmpswp8(volatile void *ptr, __int64 value, __int64 comparand) {
		return _InterlockedCompareExchange64((__int64*)ptr, value, comparand);
	}
#endif
}
