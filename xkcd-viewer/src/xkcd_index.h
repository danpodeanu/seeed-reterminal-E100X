#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include <vector>

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

// Parse a single decimal line from the on-disk index (accepts a
// value up to 100000). Returns false on malformed input or on 0 when
// `allowZero` is false (used for the count-line prefix vs entry
// lines).
bool parseUnsignedLine(String line, uint32_t& value,
                       bool allowZero = false);

// Write the given comic-number list to the SD index file atomically
// (.part + rename). Called from rebuild() and after the maintenance
// pass tops up the cache.
bool writeFile(const std::vector<int>& numbers);

// Persist the current in-memory index to disk. Convenience wrapper
// over writeFile(entries()) used after live updates like the fill
// pass.
bool persist();

// Load and validate the on-disk index. Returns false and clears the
// in-memory list on any format error so the caller can trigger a
// rebuild.
bool load();

// Walk the cache directory, keeping every comic whose metadata +
// image are both present per `isCached`. Requires `sdReady == true`
// or returns false without touching the in-memory index.
bool rebuild(bool sdReady, ComicCachedFn isCached,
             ShouldAbortFn shouldAbort = nullptr);

// Insert `number` into the sorted in-memory index if it isn't
// already present. Silently ignored when the index is not ready, or
// for the reserved comic 404. The change is in-memory only; the
// caller decides when to persist().
void addCurrent(int number);

// Pack an 8bpp indexed image (one byte per pixel, values 0..15) into
// 4bpp storage in-place, two pixels per byte. `width` must be even.
void pack4bppInPlace(uint8_t* indices, int width, int height);

}  // namespace xkcd_index
