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

void test_zen_rake_rejects_empty_and_rolls_history() {
  ZenRake rake;
  TEST_ASSERT_FALSE(rake.add(1, 1, 1, 1));
  for (size_t index = 0; index < ZenRake::kMaximumSegments; ++index) {
    TEST_ASSERT_TRUE(rake.add(index, 1, index + 1, 2));
  }
  TEST_ASSERT_TRUE(rake.add(500, 10, 510, 20));
  TEST_ASSERT_EQUAL_UINT(ZenRake::kMaximumSegments, rake.count());
  TEST_ASSERT_EQUAL_INT(1, rake.segment(0).x1);
  TEST_ASSERT_EQUAL_INT(500, rake.segment(rake.count() - 1).x1);
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

void test_kaleidoscope_rolls_segments() {
  Kaleidoscope kaleidoscope;
  for (size_t index = 0; index < Kaleidoscope::kMaximumSegments; ++index) {
    TEST_ASSERT_TRUE(kaleidoscope.add(index, 1, index + 1, 2));
  }
  TEST_ASSERT_TRUE(kaleidoscope.add(500, 10, 510, 20));
  TEST_ASSERT_EQUAL_UINT(Kaleidoscope::kMaximumSegments,
                         kaleidoscope.count());
  TEST_ASSERT_EQUAL_INT(1, kaleidoscope.segment(0).x1);
  TEST_ASSERT_EQUAL_INT(
      500, kaleidoscope.segment(kaleidoscope.count() - 1).x1);
}

void test_inkblot_restores_valid_dots() {
  Inkblot inkblot;
  TEST_ASSERT_TRUE(inkblot.add(120, 220, 14));
  Inkblot restored;
  const InkDot dots[] = {inkblot.dot(0), {100, 100, 0}};
  restored.restore(dots, 2);
  TEST_ASSERT_EQUAL_UINT(1, restored.count());
  TEST_ASSERT_EQUAL_INT(120, restored.dot(0).x);
}

void test_inkblot_rolls_dots() {
  Inkblot inkblot;
  for (size_t index = 0; index < Inkblot::kMaximumDots; ++index) {
    TEST_ASSERT_TRUE(inkblot.add(index, 200, 10));
  }
  TEST_ASSERT_TRUE(inkblot.add(500, 300, 12));
  TEST_ASSERT_EQUAL_UINT(Inkblot::kMaximumDots, inkblot.count());
  TEST_ASSERT_EQUAL_INT(1, inkblot.dot(0).x);
  TEST_ASSERT_EQUAL_INT(500, inkblot.dot(inkblot.count() - 1).x);
}

void test_pebble_stack_clamps_offsets() {
  PebbleStack pebbles;
  TEST_ASSERT_TRUE(pebbles.add(200));
  TEST_ASSERT_EQUAL_INT(80, pebbles.offset(0));
  for (size_t index = 1; index < PebbleStack::kMaximumPebbles; ++index) {
    TEST_ASSERT_TRUE(pebbles.add(0));
  }
  TEST_ASSERT_FALSE(pebbles.add(0));
}

void test_worry_stone_counts_rubs() {
  WorryStone stone;
  TEST_ASSERT_TRUE(stone.rub());
  TEST_ASSERT_TRUE(stone.rub());
  TEST_ASSERT_EQUAL_UINT16(2, stone.rubs());
  stone.reset();
  TEST_ASSERT_EQUAL_UINT16(0, stone.rubs());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bubble_wrap_pops_once_and_restores);
  RUN_TEST(test_bubble_wrap_detects_completion);
  RUN_TEST(test_zen_rake_rejects_empty_and_rolls_history);
  RUN_TEST(test_flip_dots_toggle_and_restore);
  RUN_TEST(test_ripples_age_out);
  RUN_TEST(test_counter_saturates_and_resets);
  RUN_TEST(test_kaleidoscope_rolls_segments);
  RUN_TEST(test_inkblot_restores_valid_dots);
  RUN_TEST(test_inkblot_rolls_dots);
  RUN_TEST(test_pebble_stack_clamps_offsets);
  RUN_TEST(test_worry_stone_counts_rubs);
  return UNITY_END();
}
