#include <unity.h>

#include "fidget_activities.h"

using namespace sticky_fiddle;

void setUp() {}
void tearDown() {}

void test_bubble_wrap_pops_once_and_restores() {
  BubbleWrap bubbles;
  TEST_ASSERT_TRUE(bubbles.pop(2, 3));
  TEST_ASSERT_FALSE(bubbles.pop(2, 3));
  TEST_ASSERT_TRUE(bubbles.popped(2, 3));

  BubbleWrap restored;
  restored.restore(bubbles.snapshot());
  TEST_ASSERT_TRUE(restored.popped(2, 3));
  restored.reset();
  TEST_ASSERT_FALSE(restored.popped(2, 3));
}

void test_bubble_wrap_detects_completion() {
  BubbleWrap bubbles;
  for (int row = 0; row < BubbleWrap::kRows; ++row) {
    for (int column = 0; column < BubbleWrap::kColumns; ++column) {
      TEST_ASSERT_TRUE(bubbles.pop(row, column));
    }
  }
  TEST_ASSERT_TRUE(bubbles.allPopped());
}

void test_zen_rake_rejects_empty_and_caps_history() {
  ZenRake rake;
  TEST_ASSERT_FALSE(rake.add(1, 1, 1, 1));
  for (size_t index = 0; index < ZenRake::kMaximumSegments; ++index) {
    TEST_ASSERT_TRUE(rake.add(index, 1, index + 1, 2));
  }
  TEST_ASSERT_FALSE(rake.add(0, 0, 1, 1));
  TEST_ASSERT_EQUAL_UINT(ZenRake::kMaximumSegments, rake.count());
}

void test_flip_dots_toggle_and_restore() {
  FlipDots dots;
  TEST_ASSERT_TRUE(dots.toggle(11, 7));
  TEST_ASSERT_TRUE(dots.on(11, 7));
  TEST_ASSERT_TRUE(dots.toggle(11, 7));
  TEST_ASSERT_FALSE(dots.on(11, 7));
  TEST_ASSERT_FALSE(dots.toggle(-1, 0));
}

void test_ripples_age_out() {
  RipplePond pond;
  pond.add(100, 200);
  TEST_ASSERT_EQUAL_UINT(1, pond.count());
  for (uint8_t age = 0; age < RipplePond::kMaximumAge; ++age) {
    TEST_ASSERT_TRUE(pond.advance());
  }
  TEST_ASSERT_EQUAL_UINT(0, pond.count());
}

void test_counter_saturates_and_resets() {
  PointlessCounter counter;
  counter.restore(UINT32_MAX);
  TEST_ASSERT_FALSE(counter.increment());
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, counter.value());
  counter.reset();
  TEST_ASSERT_EQUAL_UINT32(0, counter.value());
}

void test_squish_inflates_and_relaxes() {
  Squish squish;
  for (uint8_t level = 0; level < Squish::kMaximumLevel; ++level) {
    TEST_ASSERT_TRUE(squish.inflate());
  }
  TEST_ASSERT_FALSE(squish.inflate());
  for (uint8_t level = 0; level < Squish::kMaximumLevel; ++level) {
    TEST_ASSERT_TRUE(squish.relax());
  }
  TEST_ASSERT_FALSE(squish.relax());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bubble_wrap_pops_once_and_restores);
  RUN_TEST(test_bubble_wrap_detects_completion);
  RUN_TEST(test_zen_rake_rejects_empty_and_caps_history);
  RUN_TEST(test_flip_dots_toggle_and_restore);
  RUN_TEST(test_ripples_age_out);
  RUN_TEST(test_counter_saturates_and_resets);
  RUN_TEST(test_squish_inflates_and_relaxes);
  return UNITY_END();
}
