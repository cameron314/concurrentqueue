# Changelog

## [Unreleased] - 2026-10-05T11:47:00+05:30

### Fixed
- Suppressed spurious GCC `-Wstringop-overflow` diagnostic warning emitted under `-O2`/`-O3` optimization levels on GCC 14+ during `enqueue_bulk` with explicit producer tokens and dereferencing/generating iterators (Issue #456).
- Added `__GNUC__ >= 7` guarded localized diagnostic pragma to `ConcurrentQueue::inner_enqueue_bulk`.

### Added
- Added unit test `enqueue_bulk_producer_token_generating_iterator` covering bulk enqueue operations with explicit producer tokens, move-only objects, and generating iterators.

### Files Affected
- `concurrentqueue.h`
- `tests/unittests/unittests.cpp`
