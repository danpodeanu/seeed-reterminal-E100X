#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "xkcd_index_pure.h"

// Comic-cache index management. xkcd-viewer keeps a text file on the
// SD card listing every comic number whose metadata + image are both
// fully cached; loading/rebuilding/appending to that list used to
// live inline in main.cpp alongside a small pixel-packing helper for
// the 4-bit panel. Extracted so main.cpp only holds high-level flow.
namespace xkcd_index {

// Callback returning true when the caller believes comic `number` has
// both its metadata and image cached to SD (matches the semantics of
// xkcd-viewer's own comicFullyCached helper).
using ComicCachedFn = bool (*)(int number);

// Cancellation callback for rebuild(): returning true from it aborts
// the scan and preserves whatever index was previously loaded. Pass
// nullptr to run to completion.
using ShouldAbortFn = bool (*)();

// True once load() or rebuild() has installed an index.
bool ready();

// Number of entries currently held in the in-memory index (zero
// before load()/rebuild() populate it).
uint32_t count();

// Const view over the sorted, deduplicated comic-number list. Valid
// only while no other xkcd_index call is in flight.
const std::vector<int>& entries();

// Const view over the sorted, deduplicated list of comic numbers
// that have been marked as permanently non-retriable (e.g. they use
// an image format the firmware cannot decode). Random selectors
// consult skipped() to avoid wasting attempts on them.
const std::vector<int>& skips();

// O(log n) membership test against the skip list.
bool skipped(int number);

// Write the given cached + skipped comic-number lists to the SD
// index file atomically (.part + rename). Called from rebuild() and
// after the maintenance pass tops up the cache.
bool writeFile(const std::vector<int>& cached,
               const std::vector<int>& skipped);

// Persist the current in-memory index (both cached and skipped
// sections) to disk. Convenience wrapper over writeFile() used after
// live updates like the fill pass or after markSkipped().
bool persist();

// Load and validate the on-disk index. Returns false and clears the
// in-memory lists on any format error so the caller can trigger a
// rebuild.
bool load();

// Walk the cache directory, keeping every comic whose metadata +
// image are both present per `isCached`. Requires `sdReady == true`
// or returns false without touching the in-memory index. The skip
// list is preserved across rebuilds when this call is entered with a
// non-empty in-memory skip set; if load() failed and skips() is
// empty going in, they remain empty and are re-detected lazily.
bool rebuild(bool sdReady, ComicCachedFn isCached,
             ShouldAbortFn shouldAbort = nullptr);

// Insert `number` into the sorted in-memory index if it isn't
// already present. Silently ignored when the index is not ready, or
// for the reserved comic 404. The change is in-memory only; the
// caller decides when to persist().
void addCurrent(int number);

// Insert `number` into the sorted in-memory skip list if it isn't
// already present, then persist the whole index so the verdict
// survives power loss. Silently ignored for the reserved comic 404
// and for non-positive numbers. Returns true when the persist
// succeeded (or the number was already recorded); false when persist
// failed.
bool markSkipped(int number);

// Pack an 8bpp indexed image (one byte per pixel, values 0..15) into
// 4bpp storage in-place, two pixels per byte. `width` must be even.
// Defined inline in xkcd_index_pure.h so unit tests can call it on the
// native platform without linking this translation unit.

}  // namespace xkcd_index
