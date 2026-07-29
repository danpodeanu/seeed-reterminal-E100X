#include "weather_quotes.h"

#include "generated/weather_quotes_data.h"
#include "weather_quotes_bucket.h"

#include <TFT_eSPI.h>
#include <pgmspace.h>

namespace weather_quotes {

using generated::Bucket;
using generated::BucketRef;
using generated::kBuckets;

namespace {

// Thin adapter that turns the pure BucketIndex into the generated
// Bucket enum. Index values are guaranteed identical by the header's
// stay-in-sync contract (pinned by a native regression test).
Bucket wmoToBucket(int wmoCode) {
  return static_cast<Bucket>(pure::wmoToBucketIndex(wmoCode));
}

// Deterministic 32-bit mixer. We use it to bounce a running counter
// through the seed so the visit order is different for every seed but
// still hits every slot exactly once when combined with the linear
// probe below.
uint32_t mix32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// Walk the bucket in an order derived from `seed`, returning the first
// entry whose textWidth() fits in `availPx`. Coprime step keeps the
// walk from cycling early on any bucket size; the mixer scrambles the
// step and start across seeds so a jammed slot doesn't stick.
const char* firstThatFits(const BucketRef& bucket, uint32_t seed,
                          TFT_eSPI& epaper, int availPx) {
  if (bucket.count == 0 || bucket.quotes == nullptr) return nullptr;
  const uint32_t n = static_cast<uint32_t>(bucket.count);
  const uint32_t start = mix32(seed) % n;
  // Any odd number coprime with n works; when n happens to be a power
  // of two, "any odd" is enough. For odd n a step of 1 is already
  // coprime; for even n we force odd via `| 1`.
  const uint32_t step = ((mix32(seed ^ 0x9e3779b9u) % n) | 1u);
  uint32_t idx = start;
  for (uint32_t i = 0; i < n; ++i) {
    const char* candidate = bucket.quotes[idx];
    if (candidate && epaper.textWidth(candidate) <= availPx) {
      return candidate;
    }
    idx = (idx + step) % n;
  }
  return nullptr;
}

}  // namespace

const char* pickForWmo(int wmoCode, uint32_t seed, TFT_eSPI& epaper,
                       int availPx) {
  if (availPx <= 0) return nullptr;
  const Bucket primary = wmoToBucket(wmoCode);
  const char* q = firstThatFits(kBuckets[primary], seed, epaper, availPx);
  if (q) return q;
  // Fall back to the condition-agnostic bucket if nothing in the
  // primary bucket fits at the current width. Reseed slightly so the
  // fallback isn't stuck on the same rejected pattern.
  if (primary != generated::BUCKET_UNIVERSAL) {
    q = firstThatFits(kBuckets[generated::BUCKET_UNIVERSAL],
                      seed ^ 0xdeadbeefu, epaper, availPx);
  }
  return q;
}

}  // namespace weather_quotes
