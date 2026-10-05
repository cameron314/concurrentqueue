# Changelog

## [Unreleased] - 2026-10-05T23:09:00+05:30

### Added
- Added standard C++20 `std::counting_semaphore` implementation of `moodycamel::details::Semaphore` in `lightweightsemaphore.h`, enabling portable cross-platform semaphore synchronization without requiring OS-specific APIs (`CreateSemaphoreW`, `mach/mach.h`, or `semaphore.h`) (Issue #449).
- Added opt-out / opt-in configuration macros `MOODYCAMEL_USE_OS_SEMAPHORE` and `MOODYCAMEL_USE_STD_SEMAPHORE`.
- Added unit test `details_semaphore_basic_and_threaded` in `tests/unittests/unittests.cpp` validating non-blocking try-wait, timed wait, single and multi-signal, and concurrent thread signaling.

### Files Affected
- `lightweightsemaphore.h`
- `tests/unittests/unittests.cpp`
