#include <unity.h>

#include "lights_out_game.h"

void setUp() {}
void tearDown() {}

void test_corner_press_toggles_three_cells() {
  LightsOutGame game;
  game.startPuzzle(1UL << 0);

  const uint32_t expected =
      (1UL << 0) | (1UL << 1) | (1UL << LightsOutGame::kSize);
  TEST_ASSERT_EQUAL_UINT32(expected, game.cells());
}

void test_center_press_toggles_five_cells() {
  LightsOutGame game;
  constexpr int center = LightsOutGame::kCellCount / 2;
  game.startPuzzle(1UL << center);

  const uint32_t expected =
      (1UL << center) |
      (1UL << (center - LightsOutGame::kSize)) |
      (1UL << (center + LightsOutGame::kSize)) |
      (1UL << (center - 1)) |
      (1UL << (center + 1));
  TEST_ASSERT_EQUAL_UINT32(expected, game.cells());
}

void test_scramble_is_solved_by_replaying_its_press_mask() {
  LightsOutGame game;
  game.startPuzzle(0x0125AA15UL);

  const uint32_t solution = game.solutionMask();
  for (int index = 0; index < LightsOutGame::kCellCount; ++index) {
    if ((solution & (1UL << index)) != 0) {
      TEST_ASSERT_TRUE(
          game.press(index / LightsOutGame::kSize,
                     index % LightsOutGame::kSize));
    }
  }
  TEST_ASSERT_TRUE(game.solved());
}

void test_reset_restores_initial_board_and_move_count() {
  LightsOutGame game;
  game.startPuzzle(0x155UL);
  const uint32_t initial = game.cells();

  TEST_ASSERT_TRUE(game.press(2, 2));
  TEST_ASSERT_EQUAL_UINT16(1, game.moves());
  game.reset();

  TEST_ASSERT_EQUAL_UINT32(initial, game.cells());
  TEST_ASSERT_EQUAL_UINT16(0, game.moves());
}

void test_invalid_press_does_not_change_game() {
  LightsOutGame game;
  game.startPuzzle(0x155UL);
  const uint32_t initial = game.cells();

  TEST_ASSERT_FALSE(game.press(-1, 0));
  TEST_ASSERT_FALSE(game.press(0, LightsOutGame::kSize));
  TEST_ASSERT_EQUAL_UINT32(initial, game.cells());
  TEST_ASSERT_EQUAL_UINT16(0, game.moves());
}

void test_zero_scramble_still_creates_a_puzzle() {
  LightsOutGame game;
  game.startPuzzle(0);

  TEST_ASSERT_FALSE(game.solved());
  TEST_ASSERT_NOT_EQUAL_UINT32(0, game.solutionMask());
}

void test_snapshot_restores_board_and_moves() {
  LightsOutGame original;
  original.startPuzzle(0x155UL);
  original.press(1, 3);
  original.press(4, 0);
  const LightsOutGame::Snapshot snapshot = original.snapshot();

  LightsOutGame restored;
  TEST_ASSERT_TRUE(restored.restore(snapshot));
  TEST_ASSERT_EQUAL_UINT32(original.cells(), restored.cells());
  TEST_ASSERT_EQUAL_UINT32(original.solutionMask(),
                           restored.solutionMask());
  TEST_ASSERT_EQUAL_UINT16(original.moves(), restored.moves());

  restored.reset();
  TEST_ASSERT_EQUAL_UINT32(snapshot.initialCells, restored.cells());
  TEST_ASSERT_EQUAL_UINT16(0, restored.moves());
}

void test_invalid_snapshot_is_rejected() {
  LightsOutGame game;
  const LightsOutGame::Snapshot invalid = {
      0,
      0,
      0,
      42,
  };
  TEST_ASSERT_FALSE(game.restore(invalid));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_corner_press_toggles_three_cells);
  RUN_TEST(test_center_press_toggles_five_cells);
  RUN_TEST(test_scramble_is_solved_by_replaying_its_press_mask);
  RUN_TEST(test_reset_restores_initial_board_and_move_count);
  RUN_TEST(test_invalid_press_does_not_change_game);
  RUN_TEST(test_zero_scramble_still_creates_a_puzzle);
  RUN_TEST(test_snapshot_restores_board_and_moves);
  RUN_TEST(test_invalid_snapshot_is_rejected);
  return UNITY_END();
}
