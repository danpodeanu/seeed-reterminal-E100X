#include <unity.h>

#include "dots_and_boxes_game.h"
#include "game_2048.h"
#include "game_progress_store.h"
#include "lights_out_game.h"
#include "mini_minesweeper_game.h"
#include "nonogram_game.h"
#include "peg_solitaire_game.h"
#include "pipe_connect_game.h"
#include "reversi_game.h"
#include "slitherlink_game.h"
#include "sokoban_game.h"

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

Game2048::Snapshot snapshot2048With(
    const uint32_t (&cells)[Game2048::kCellCount], uint32_t score = 0,
    uint32_t bestScore = 0) {
  Game2048::Snapshot snapshot = {};
  for (int index = 0; index < Game2048::kCellCount; ++index) {
    snapshot.cells[index] = cells[index];
  }
  snapshot.score = score;
  snapshot.bestScore = bestScore;
  return snapshot;
}

void test_2048_start_adds_two_tiles() {
  Game2048 game;
  game.start(0, 1);

  int populated = 0;
  for (int row = 0; row < Game2048::kSize; ++row) {
    for (int column = 0; column < Game2048::kSize; ++column) {
      if (game.at(row, column) != 0) ++populated;
    }
  }
  TEST_ASSERT_EQUAL_INT(2, populated);
}

void test_2048_left_move_merges_each_pair_once() {
  constexpr uint32_t cells[Game2048::kCellCount] = {
      2, 2, 2, 2,
      0, 0, 0, 0,
      0, 0, 0, 0,
      0, 0, 0, 0,
  };
  Game2048 game;
  TEST_ASSERT_TRUE(game.restore(snapshot2048With(cells)));
  TEST_ASSERT_TRUE(game.move(Game2048::Direction::Left, 15));

  TEST_ASSERT_EQUAL_UINT32(4, game.at(0, 0));
  TEST_ASSERT_EQUAL_UINT32(4, game.at(0, 1));
  TEST_ASSERT_EQUAL_UINT32(8, game.score());
}

void test_2048_right_move_preserves_direction() {
  constexpr uint32_t cells[Game2048::kCellCount] = {
      2, 0, 2, 4,
      0, 0, 0, 0,
      0, 0, 0, 0,
      0, 0, 0, 0,
  };
  Game2048 game;
  TEST_ASSERT_TRUE(game.restore(snapshot2048With(cells)));
  TEST_ASSERT_TRUE(game.move(Game2048::Direction::Right, 0));

  TEST_ASSERT_EQUAL_UINT32(4, game.at(0, 2));
  TEST_ASSERT_EQUAL_UINT32(4, game.at(0, 3));
}

void test_2048_up_move_merges_columns() {
  constexpr uint32_t cells[Game2048::kCellCount] = {
      2, 0, 0, 0,
      2, 0, 0, 0,
      4, 0, 0, 0,
      4, 0, 0, 0,
  };
  Game2048 game;
  TEST_ASSERT_TRUE(game.restore(snapshot2048With(cells)));
  TEST_ASSERT_TRUE(game.move(Game2048::Direction::Up, 15));

  TEST_ASSERT_EQUAL_UINT32(4, game.at(0, 0));
  TEST_ASSERT_EQUAL_UINT32(8, game.at(1, 0));
  TEST_ASSERT_EQUAL_UINT32(12, game.score());
}

void test_2048_no_op_move_does_not_add_tile() {
  constexpr uint32_t cells[Game2048::kCellCount] = {
      2, 4, 8, 16,
      0, 0, 0, 0,
      0, 0, 0, 0,
      0, 0, 0, 0,
  };
  Game2048 game;
  TEST_ASSERT_TRUE(game.restore(snapshot2048With(cells)));
  TEST_ASSERT_FALSE(game.move(Game2048::Direction::Left, 7));

  TEST_ASSERT_EQUAL_UINT32(0, game.at(1, 0));
  TEST_ASSERT_EQUAL_UINT32(0, game.score());
}

void test_2048_full_board_without_merges_is_game_over() {
  constexpr uint32_t cells[Game2048::kCellCount] = {
      2, 4, 2, 4,
      4, 2, 4, 2,
      2, 4, 2, 4,
      4, 2, 4, 8,
  };
  Game2048 game;
  TEST_ASSERT_TRUE(game.restore(snapshot2048With(cells)));
  TEST_ASSERT_TRUE(game.gameOver());
  TEST_ASSERT_FALSE(game.move(Game2048::Direction::Left, 0));
}

void test_2048_snapshot_restores_score_and_win() {
  constexpr uint32_t cells[Game2048::kCellCount] = {
      2048, 4, 0, 0,
      0, 0, 0, 0,
      0, 0, 0, 0,
      0, 0, 0, 0,
  };
  Game2048 game;
  TEST_ASSERT_TRUE(game.restore(snapshot2048With(cells, 4096, 8192)));

  TEST_ASSERT_TRUE(game.won());
  TEST_ASSERT_EQUAL_UINT32(4096, game.score());
  TEST_ASSERT_EQUAL_UINT32(8192, game.bestScore());
}

void test_2048_invalid_snapshot_is_rejected() {
  constexpr uint32_t cells[Game2048::kCellCount] = {
      3, 0, 0, 0,
      0, 0, 0, 0,
      0, 0, 0, 0,
      0, 0, 0, 0,
  };
  Game2048 game;
  TEST_ASSERT_FALSE(game.restore(snapshot2048With(cells)));
}

void test_pipe_connect_generation_creates_scrambled_tree() {
  PipeConnectGame game;
  game.start(0x12345678UL);

  int edgeEnds = 0;
  for (int row = 0; row < PipeConnectGame::kSize; ++row) {
    for (int column = 0; column < PipeConnectGame::kSize; ++column) {
      uint8_t mask = game.solutionAt(row, column);
      TEST_ASSERT_NOT_EQUAL_UINT8(0, mask);
      while (mask != 0) {
        edgeEnds += mask & 1U;
        mask >>= 1;
      }
    }
  }
  TEST_ASSERT_EQUAL_INT((PipeConnectGame::kCellCount - 1) * 2, edgeEnds);
  TEST_ASSERT_FALSE(game.solved());
}

void test_pipe_connect_four_rotations_restore_tile() {
  PipeConnectGame game;
  game.start(7);
  const uint8_t original = game.at(2, 3);

  for (int turn = 0; turn < 4; ++turn) {
    TEST_ASSERT_TRUE(game.rotate(2, 3));
  }

  TEST_ASSERT_EQUAL_UINT8(original, game.at(2, 3));
  TEST_ASSERT_EQUAL_UINT16(4, game.moves());
}

void test_pipe_connect_solution_connects_every_tile() {
  PipeConnectGame game;
  game.start(0xCAFEBABEUL);
  PipeConnectGame::Snapshot snapshot = game.snapshot();
  for (int index = 0; index < PipeConnectGame::kCellCount; ++index) {
    snapshot.cells[index] = snapshot.solutionCells[index];
  }

  PipeConnectGame solved;
  TEST_ASSERT_TRUE(solved.restore(snapshot));
  TEST_ASSERT_TRUE(solved.solved());
}

void test_pipe_connect_reset_restores_scramble() {
  PipeConnectGame game;
  game.start(99);
  const PipeConnectGame::Snapshot initial = game.snapshot();
  game.rotate(0, 0);
  game.rotate(4, 5);
  game.reset();

  for (int index = 0; index < PipeConnectGame::kCellCount; ++index) {
    TEST_ASSERT_EQUAL_UINT8(initial.cells[index],
                            game.snapshot().cells[index]);
  }
  TEST_ASSERT_EQUAL_UINT16(0, game.moves());
  TEST_ASSERT_FALSE(game.solved());
}

void test_pipe_connect_snapshot_restores_progress() {
  PipeConnectGame original;
  original.start(314159);
  original.rotate(1, 1);
  original.rotate(3, 4);
  const PipeConnectGame::Snapshot snapshot = original.snapshot();

  PipeConnectGame restored;
  TEST_ASSERT_TRUE(restored.restore(snapshot));
  TEST_ASSERT_EQUAL_UINT16(original.moves(), restored.moves());
  for (int row = 0; row < PipeConnectGame::kSize; ++row) {
    for (int column = 0; column < PipeConnectGame::kSize; ++column) {
      TEST_ASSERT_EQUAL_UINT8(original.at(row, column),
                              restored.at(row, column));
    }
  }
}

void test_pipe_connect_invalid_snapshot_is_rejected() {
  PipeConnectGame game;
  game.start(42);
  PipeConnectGame::Snapshot snapshot = game.snapshot();
  snapshot.solutionCells[0] = 0;

  PipeConnectGame restored;
  TEST_ASSERT_FALSE(restored.restore(snapshot));
}

void test_pipe_connect_many_seeds_are_solvable_and_scrambled() {
  for (uint32_t seed = 0; seed < 100; ++seed) {
    PipeConnectGame game;
    game.start(seed);
    TEST_ASSERT_FALSE(game.solved());

    PipeConnectGame::Snapshot snapshot = game.snapshot();
    for (int index = 0; index < PipeConnectGame::kCellCount; ++index) {
      snapshot.cells[index] = snapshot.solutionCells[index];
    }
    PipeConnectGame solved;
    TEST_ASSERT_TRUE(solved.restore(snapshot));
    TEST_ASSERT_TRUE(solved.solved());
  }
}

void test_minesweeper_first_reveal_is_safe_and_opens_area() {
  MiniMinesweeperGame game;
  game.start(1234);

  TEST_ASSERT_EQUAL(
      MiniMinesweeperGame::RevealResult::Revealed, game.reveal(2, 2));
  TEST_ASSERT_TRUE(game.generated());
  TEST_ASSERT_FALSE(game.isMine(2, 2));
  TEST_ASSERT_TRUE(game.isRevealed(2, 2));
  TEST_ASSERT_GREATER_THAN_INT(1, game.revealedCount());
}

void test_minesweeper_first_reveal_excludes_neighbors() {
  MiniMinesweeperGame game;
  game.start(5678);
  game.reveal(3, 3);

  for (int row = 2; row <= 4; ++row) {
    for (int column = 2; column <= 4; ++column) {
      TEST_ASSERT_FALSE(game.isMine(row, column));
    }
  }
}

void test_minesweeper_generates_six_mines() {
  MiniMinesweeperGame game;
  game.start(42);
  game.reveal(0, 0);

  int mines = 0;
  for (int row = 0; row < MiniMinesweeperGame::kSize; ++row) {
    for (int column = 0; column < MiniMinesweeperGame::kSize; ++column) {
      if (game.isMine(row, column)) ++mines;
    }
  }
  TEST_ASSERT_EQUAL_INT(MiniMinesweeperGame::kMineCount, mines);
}

void test_minesweeper_flag_blocks_reveal() {
  MiniMinesweeperGame game;
  game.start(77);
  TEST_ASSERT_TRUE(game.toggleFlag(1, 1));
  TEST_ASSERT_TRUE(game.isFlagged(1, 1));
  TEST_ASSERT_EQUAL(MiniMinesweeperGame::RevealResult::NoChange,
                    game.reveal(1, 1));
  TEST_ASSERT_FALSE(game.isRevealed(1, 1));
  TEST_ASSERT_TRUE(game.toggleFlag(1, 1));
  TEST_ASSERT_FALSE(game.isFlagged(1, 1));
}

void test_minesweeper_revealing_mine_loses() {
  MiniMinesweeperGame game;
  game.start(2024);
  game.reveal(0, 0);

  for (int row = 0; row < MiniMinesweeperGame::kSize; ++row) {
    for (int column = 0; column < MiniMinesweeperGame::kSize; ++column) {
      if (!game.isMine(row, column)) continue;
      TEST_ASSERT_EQUAL(MiniMinesweeperGame::RevealResult::Lost,
                        game.reveal(row, column));
      TEST_ASSERT_TRUE(game.lost());
      TEST_ASSERT_TRUE(game.gameOver());
      return;
    }
  }
  TEST_FAIL_MESSAGE("generated board had no mine");
}

void test_minesweeper_complete_safe_board_wins() {
  MiniMinesweeperGame game;
  game.start(13579);
  game.reveal(2, 2);
  MiniMinesweeperGame::Snapshot snapshot = game.snapshot();
  snapshot.revealed =
      MiniMinesweeperGame::kCellMask & ~snapshot.mines;
  snapshot.flagged = 0;

  MiniMinesweeperGame won;
  TEST_ASSERT_TRUE(won.restore(snapshot));
  TEST_ASSERT_TRUE(won.won());
  TEST_ASSERT_TRUE(won.gameOver());
}

void test_minesweeper_reset_keeps_board_and_clears_progress() {
  MiniMinesweeperGame game;
  game.start(2468);
  game.reveal(1, 1);
  const uint64_t mines = game.snapshot().mines;
  game.toggleFlag(5, 5);
  game.reset();

  TEST_ASSERT_EQUAL_UINT64(mines, game.snapshot().mines);
  TEST_ASSERT_TRUE(game.generated());
  TEST_ASSERT_EQUAL_INT(0, game.revealedCount());
  TEST_ASSERT_EQUAL_INT(0, game.flags());
}

void test_minesweeper_snapshot_restores_progress() {
  MiniMinesweeperGame original;
  original.start(999);
  original.reveal(3, 3);
  original.toggleFlag(0, 0);

  MiniMinesweeperGame restored;
  TEST_ASSERT_TRUE(restored.restore(original.snapshot()));
  TEST_ASSERT_EQUAL_UINT64(original.snapshot().mines,
                           restored.snapshot().mines);
  TEST_ASSERT_EQUAL_UINT64(original.snapshot().revealed,
                           restored.snapshot().revealed);
  TEST_ASSERT_EQUAL_UINT64(original.snapshot().flagged,
                           restored.snapshot().flagged);
}

void test_minesweeper_invalid_snapshot_is_rejected() {
  MiniMinesweeperGame game;
  game.start(88);
  MiniMinesweeperGame::Snapshot snapshot = game.snapshot();
  snapshot.revealed = 1;
  snapshot.flagged = 1;

  MiniMinesweeperGame restored;
  TEST_ASSERT_FALSE(restored.restore(snapshot));
}

MiniMinesweeperGame::Snapshot minesweeperChordSnapshot(uint64_t flags) {
  MiniMinesweeperGame::Snapshot snapshot = {};
  snapshot.mines = (1ULL << 0) | (1ULL << 5) | (1ULL << 14) |
                   (1ULL << 21) | (1ULL << 30) | (1ULL << 35);
  snapshot.revealed = 1ULL << 7;
  snapshot.flagged = flags;
  snapshot.seed = 1;
  snapshot.generated = 1;
  return snapshot;
}

void test_minesweeper_revealed_number_opens_neighbors_when_fulfilled() {
  MiniMinesweeperGame game;
  TEST_ASSERT_TRUE(
      game.restore(minesweeperChordSnapshot((1ULL << 0) | (1ULL << 14))));
  TEST_ASSERT_EQUAL_INT(2, game.adjacentMines(1, 1));

  const MiniMinesweeperGame::RevealResult result = game.reveal(1, 1);

  TEST_ASSERT_NOT_EQUAL(MiniMinesweeperGame::RevealResult::NoChange, result);
  TEST_ASSERT_NOT_EQUAL(MiniMinesweeperGame::RevealResult::Lost, result);
  TEST_ASSERT_TRUE(game.isRevealed(0, 1));
  TEST_ASSERT_TRUE(game.isRevealed(1, 0));
  TEST_ASSERT_TRUE(game.isRevealed(1, 2));
  TEST_ASSERT_FALSE(game.lost());
}

void test_minesweeper_revealed_number_waits_for_enough_flags() {
  MiniMinesweeperGame game;
  TEST_ASSERT_TRUE(game.restore(minesweeperChordSnapshot(1ULL << 0)));

  TEST_ASSERT_EQUAL(MiniMinesweeperGame::RevealResult::NoChange,
                    game.reveal(1, 1));
  TEST_ASSERT_FALSE(game.isRevealed(0, 1));
}

void test_minesweeper_incorrect_fulfilled_flags_can_hit_mine() {
  MiniMinesweeperGame game;
  TEST_ASSERT_TRUE(
      game.restore(minesweeperChordSnapshot((1ULL << 0) | (1ULL << 1))));

  TEST_ASSERT_EQUAL(MiniMinesweeperGame::RevealResult::Lost,
                    game.reveal(1, 1));
  TEST_ASSERT_TRUE(game.isRevealed(2, 2));
  TEST_ASSERT_TRUE(game.lost());
}

void test_nonogram_cycles_blank_filled_crossed_blank() {
  NonogramGame game;
  game.start(0x15555UL);

  TEST_ASSERT_EQUAL(NonogramGame::CellState::Blank, game.at(1, 2));
  TEST_ASSERT_TRUE(game.cycle(1, 2));
  TEST_ASSERT_EQUAL(NonogramGame::CellState::Filled, game.at(1, 2));
  TEST_ASSERT_TRUE(game.cycle(1, 2));
  TEST_ASSERT_EQUAL(NonogramGame::CellState::Crossed, game.at(1, 2));
  TEST_ASSERT_TRUE(game.cycle(1, 2));
  TEST_ASSERT_EQUAL(NonogramGame::CellState::Blank, game.at(1, 2));
}

void test_nonogram_matches_only_filled_solution_cells() {
  constexpr uint32_t solution = (1UL << 0) | (1UL << 6) | (1UL << 12);
  NonogramGame game;
  game.start(solution);

  game.cycle(0, 0);
  game.cycle(1, 1);
  game.cycle(2, 2);
  TEST_ASSERT_TRUE(game.solved());
  TEST_ASSERT_FALSE(game.cycle(4, 4));
}

void test_nonogram_extra_filled_cell_prevents_completion() {
  NonogramGame game;
  game.start(1UL << 0);
  game.cycle(4, 4);
  game.cycle(0, 0);

  TEST_ASSERT_FALSE(game.solved());
}

void test_nonogram_generates_row_and_column_clues() {
  constexpr uint32_t solution =
      (1UL << 0) | (1UL << 2) | (1UL << 3) |
      (1UL << 7) | (1UL << 12) | (1UL << 17) | (1UL << 22);
  NonogramGame game;
  game.start(solution);
  uint8_t clues[NonogramGame::kSize] = {};

  TEST_ASSERT_EQUAL_INT(2, game.rowClues(0, clues));
  TEST_ASSERT_EQUAL_UINT8(1, clues[0]);
  TEST_ASSERT_EQUAL_UINT8(2, clues[1]);
  TEST_ASSERT_EQUAL_INT(1, game.columnClues(2, clues));
  TEST_ASSERT_EQUAL_UINT8(5, clues[0]);
}

void test_nonogram_reset_clears_marks_but_keeps_solution() {
  NonogramGame game;
  game.start(0x1F1F1FUL);
  game.cycle(2, 2);
  const uint32_t solution = game.snapshot().solution;
  game.reset();

  TEST_ASSERT_EQUAL_UINT32(solution, game.snapshot().solution);
  TEST_ASSERT_EQUAL_UINT64(0, game.snapshot().cells);
}

void test_nonogram_snapshot_restores_progress() {
  NonogramGame original;
  original.start(0x15555UL);
  original.cycle(0, 0);
  original.cycle(1, 1);
  original.cycle(1, 1);

  NonogramGame restored;
  TEST_ASSERT_TRUE(restored.restore(original.snapshot()));
  TEST_ASSERT_EQUAL_UINT32(original.snapshot().solution,
                           restored.snapshot().solution);
  TEST_ASSERT_EQUAL_UINT64(original.snapshot().cells,
                           restored.snapshot().cells);
}

void test_nonogram_invalid_snapshot_is_rejected() {
  NonogramGame game;
  const NonogramGame::Snapshot invalid = {
      1,
      3,
  };
  TEST_ASSERT_FALSE(game.restore(invalid));
}

void test_reversi_starts_with_four_discs_and_four_legal_moves() {
  ReversiGame game;
  game.start();

  TEST_ASSERT_EQUAL(ReversiGame::Disc::White, game.at(3, 3));
  TEST_ASSERT_EQUAL(ReversiGame::Disc::Black, game.at(3, 4));
  TEST_ASSERT_EQUAL_INT(2, game.score(ReversiGame::Disc::Black));
  TEST_ASSERT_EQUAL_INT(2, game.score(ReversiGame::Disc::White));
  TEST_ASSERT_EQUAL_UINT64((1ULL << (2 * 8 + 3)) |
                              (1ULL << (3 * 8 + 2)) |
                              (1ULL << (4 * 8 + 5)) |
                              (1ULL << (5 * 8 + 4)),
                          game.legalMoves());
}

void test_reversi_move_places_disc_and_flips_opponent() {
  ReversiGame game;
  game.start();

  TEST_ASSERT_EQUAL(ReversiGame::MoveResult::Moved, game.play(2, 3));
  TEST_ASSERT_EQUAL(ReversiGame::Disc::Black, game.at(2, 3));
  TEST_ASSERT_EQUAL(ReversiGame::Disc::Black, game.at(3, 3));
  TEST_ASSERT_EQUAL_INT(4, game.score(ReversiGame::Disc::Black));
  TEST_ASSERT_EQUAL_INT(1, game.score(ReversiGame::Disc::White));
  TEST_ASSERT_EQUAL(ReversiGame::Disc::White, game.currentPlayer());
}

void test_reversi_move_flips_in_all_eight_directions() {
  constexpr int targetRow = 3;
  constexpr int targetColumn = 3;
  uint64_t black = 0;
  uint64_t white = 0;
  for (int rowDirection = -1; rowDirection <= 1; ++rowDirection) {
    for (int columnDirection = -1; columnDirection <= 1;
         ++columnDirection) {
      if (rowDirection == 0 && columnDirection == 0) continue;
      white |= 1ULL << ((targetRow + rowDirection) * 8 +
                       targetColumn + columnDirection);
      black |= 1ULL << ((targetRow + rowDirection * 2) * 8 +
                       targetColumn + columnDirection * 2);
    }
  }
  ReversiGame game;
  TEST_ASSERT_TRUE(game.restore(
      {black, white, static_cast<uint8_t>(ReversiGame::Disc::Black)}));

  TEST_ASSERT_EQUAL(ReversiGame::MoveResult::GameOver,
                    game.play(targetRow, targetColumn));
  TEST_ASSERT_EQUAL_INT(17, game.score(ReversiGame::Disc::Black));
  TEST_ASSERT_EQUAL_INT(0, game.score(ReversiGame::Disc::White));
}

void test_reversi_rejects_non_capturing_and_occupied_moves() {
  ReversiGame game;
  game.start();
  const ReversiGame::Snapshot initial = game.snapshot();

  TEST_ASSERT_EQUAL(ReversiGame::MoveResult::Illegal, game.play(0, 0));
  TEST_ASSERT_EQUAL(ReversiGame::MoveResult::Illegal, game.play(3, 3));
  TEST_ASSERT_EQUAL_UINT64(initial.black, game.snapshot().black);
  TEST_ASSERT_EQUAL_UINT64(initial.white, game.snapshot().white);
}

void test_reversi_full_board_is_game_over() {
  ReversiGame game;
  TEST_ASSERT_TRUE(game.restore(
      {UINT64_MAX, 0, static_cast<uint8_t>(ReversiGame::Disc::Black)}));

  TEST_ASSERT_TRUE(game.gameOver());
  TEST_ASSERT_EQUAL(ReversiGame::Disc::Black, game.winner());
}

void test_reversi_automatically_passes_when_opponent_has_no_move() {
  ReversiGame game;
  TEST_ASSERT_TRUE(game.restore(
      {0xF9C1A591ADB59FBCULL, 0x063E5A6E524A4040ULL,
       static_cast<uint8_t>(ReversiGame::Disc::White)}));

  TEST_ASSERT_EQUAL(ReversiGame::MoveResult::OpponentPassed, game.play(1, 5));
  TEST_ASSERT_EQUAL(ReversiGame::Disc::White, game.currentPlayer());
  TEST_ASSERT_FALSE(game.gameOver());
}

void test_reversi_ai_selects_a_legal_move() {
  ReversiGame game;
  game.start();
  int row = -1;
  int column = -1;

  TEST_ASSERT_TRUE(game.chooseBestMove(3, row, column));
  TEST_ASSERT_TRUE(game.isLegalMove(row, column));
}

void test_reversi_ai_prefers_an_available_corner() {
  const uint64_t black =
      (1ULL << 2) | (1ULL << (3 * 8 + 4)) | (1ULL << 63);
  const uint64_t white = (1ULL << 1) | (1ULL << (3 * 8 + 3));
  ReversiGame game;
  TEST_ASSERT_TRUE(game.restore(
      {black, white, static_cast<uint8_t>(ReversiGame::Disc::Black)}));
  TEST_ASSERT_TRUE(game.isLegalMove(0, 0));
  TEST_ASSERT_TRUE(game.isLegalMove(3, 2));
  int row = -1;
  int column = -1;

  TEST_ASSERT_TRUE(game.chooseBestMove(3, row, column));
  TEST_ASSERT_EQUAL_INT(0, row);
  TEST_ASSERT_EQUAL_INT(0, column);
}

void test_reversi_snapshot_restores_progress() {
  ReversiGame original;
  original.start();
  original.play(2, 3);

  ReversiGame restored;
  TEST_ASSERT_TRUE(restored.restore(original.snapshot()));
  TEST_ASSERT_EQUAL_UINT64(original.snapshot().black,
                           restored.snapshot().black);
  TEST_ASSERT_EQUAL_UINT64(original.snapshot().white,
                           restored.snapshot().white);
  TEST_ASSERT_EQUAL(original.currentPlayer(), restored.currentPlayer());
}

void test_reversi_invalid_snapshot_is_rejected() {
  ReversiGame game;
  TEST_ASSERT_FALSE(game.restore(
      {1, 1, static_cast<uint8_t>(ReversiGame::Disc::Black)}));
  TEST_ASSERT_FALSE(game.restore({0xFUL, 0, 0}));
}

void test_dots_and_boxes_starts_empty_with_player_one() {
  DotsAndBoxesGame game;
  game.start();

  TEST_ASSERT_EQUAL(DotsAndBoxesGame::Player::Player1,
                    game.currentPlayer());
  TEST_ASSERT_EQUAL_UINT8(0, game.moves());
  TEST_ASSERT_EQUAL_UINT8(0, game.player1Score());
  TEST_ASSERT_EQUAL_UINT8(0, game.player2Score());
  TEST_ASSERT_FALSE(game.gameOver());
}

void test_dots_and_boxes_alternates_after_uncaptured_edge() {
  DotsAndBoxesGame game;
  game.start();

  TEST_ASSERT_TRUE(game.placeHorizontal(0, 0));
  TEST_ASSERT_EQUAL(DotsAndBoxesGame::Player::Player2,
                    game.currentPlayer());
  TEST_ASSERT_FALSE(game.placeHorizontal(0, 0));
  TEST_ASSERT_EQUAL_UINT8(1, game.moves());
}

void test_dots_and_boxes_capture_grants_another_turn() {
  DotsAndBoxesGame game;
  game.start();
  TEST_ASSERT_TRUE(game.placeHorizontal(0, 0));
  TEST_ASSERT_TRUE(game.placeVertical(0, 0));
  TEST_ASSERT_TRUE(game.placeHorizontal(1, 0));

  TEST_ASSERT_TRUE(game.placeVertical(0, 1));
  TEST_ASSERT_EQUAL(DotsAndBoxesGame::Player::Player2, game.owner(0, 0));
  TEST_ASSERT_EQUAL_UINT8(1, game.player2Score());
  TEST_ASSERT_EQUAL(DotsAndBoxesGame::Player::Player2,
                    game.currentPlayer());
}

void test_dots_and_boxes_shared_edge_can_capture_two_boxes() {
  DotsAndBoxesGame game;
  const uint32_t horizontal =
      (1UL << 0) | (1UL << 1) | (1UL << 4) | (1UL << 5);
  const uint32_t vertical = (1UL << 0) | (1UL << 2);
  TEST_ASSERT_TRUE(game.restore(
      {horizontal, vertical, 0, 0,
       static_cast<uint8_t>(DotsAndBoxesGame::Player::Player1)}));

  TEST_ASSERT_TRUE(game.placeVertical(0, 1));
  TEST_ASSERT_EQUAL_UINT8(2, game.player1Score());
  TEST_ASSERT_EQUAL(DotsAndBoxesGame::Player::Player1,
                    game.currentPlayer());
}

void test_dots_and_boxes_snapshot_rejects_unowned_complete_box() {
  DotsAndBoxesGame game;
  TEST_ASSERT_FALSE(game.restore(
      {(1UL << 0) | (1UL << 4), (1UL << 0) | (1UL << 1), 0, 0,
       static_cast<uint8_t>(DotsAndBoxesGame::Player::Player1)}));
}

void test_peg_solitaire_starts_with_center_empty() {
  PegSolitaireGame game;
  game.start();

  TEST_ASSERT_EQUAL_UINT8(32, game.pegCount());
  TEST_ASSERT_FALSE(game.hasPeg(3, 3));
  TEST_ASSERT_TRUE(game.hasPeg(3, 1));
  TEST_ASSERT_TRUE(game.hasAvailableMove());
}

void test_peg_solitaire_direct_jump_removes_middle_peg() {
  PegSolitaireGame game;
  game.start();

  TEST_ASSERT_TRUE(game.move(3, 1, 3, 3));
  TEST_ASSERT_FALSE(game.hasPeg(3, 1));
  TEST_ASSERT_FALSE(game.hasPeg(3, 2));
  TEST_ASSERT_TRUE(game.hasPeg(3, 3));
  TEST_ASSERT_EQUAL_UINT8(31, game.pegCount());
  TEST_ASSERT_EQUAL_UINT8(1, game.moves());
}

void test_peg_solitaire_taps_select_and_complete_jump() {
  PegSolitaireGame game;
  game.start();

  TEST_ASSERT_EQUAL(PegSolitaireGame::TapResult::Selected, game.tap(3, 1));
  TEST_ASSERT_EQUAL_INT(3, game.selectedRow());
  TEST_ASSERT_EQUAL_INT(1, game.selectedColumn());
  TEST_ASSERT_EQUAL(PegSolitaireGame::TapResult::Moved, game.tap(3, 3));
  TEST_ASSERT_FALSE(game.hasSelection());
}

void test_peg_solitaire_rejects_invalid_jump() {
  PegSolitaireGame game;
  game.start();
  const PegSolitaireGame::Snapshot before = game.snapshot();

  TEST_ASSERT_FALSE(game.move(0, 2, 2, 2));
  TEST_ASSERT_EQUAL_UINT64(before.pegs, game.snapshot().pegs);
}

void test_peg_solitaire_one_center_peg_is_solved() {
  PegSolitaireGame game;
  TEST_ASSERT_TRUE(game.restore({1ULL << (3 * 7 + 3),
                                 PegSolitaireGame::kNoSelection}));
  TEST_ASSERT_TRUE(game.solved());
  TEST_ASSERT_FALSE(game.hasAvailableMove());
}

void test_peg_solitaire_invalid_snapshot_is_rejected() {
  PegSolitaireGame game;
  TEST_ASSERT_FALSE(game.restore({0, PegSolitaireGame::kNoSelection}));
  TEST_ASSERT_FALSE(game.restore({1ULL, PegSolitaireGame::kNoSelection}));
}

void test_sokoban_first_microban_level_has_known_solution() {
  SokobanGame game;
  game.start(0);
  constexpr char kSolution[] = "DLURRRDLULLDDRULURUULDRDDRRULDLUU";

  TEST_ASSERT_FALSE(game.solved());
  for (const char move : kSolution) {
    if (move == '\0') break;
    const SokobanGame::Direction direction =
        move == 'U'   ? SokobanGame::Direction::Up
        : move == 'R' ? SokobanGame::Direction::Right
        : move == 'D' ? SokobanGame::Direction::Down
                      : SokobanGame::Direction::Left;
    TEST_ASSERT_TRUE(game.move(direction));
  }
  TEST_ASSERT_TRUE(game.solved());
  TEST_ASSERT_EQUAL_UINT16(sizeof(kSolution) - 1, game.moveCount());
}

void test_sokoban_rejects_wall_and_resets_progress() {
  SokobanGame game;
  game.start(0);

  TEST_ASSERT_FALSE(game.move(SokobanGame::Direction::Left));
  TEST_ASSERT_TRUE(game.move(SokobanGame::Direction::Up));
  game.reset();
  TEST_ASSERT_FALSE(game.solved());
  TEST_ASSERT_EQUAL_UINT16(0, game.moveCount());
  TEST_ASSERT_EQUAL_UINT16(0, game.pushCount());
}

void test_sokoban_next_level_and_snapshot_restore() {
  SokobanGame original;
  original.start(0);
  original.nextLevel();
  TEST_ASSERT_EQUAL_UINT16(1, original.levelIndex());
  TEST_ASSERT_TRUE(original.move(SokobanGame::Direction::Right));

  SokobanGame restored;
  TEST_ASSERT_TRUE(restored.restore(original.snapshot()));
  TEST_ASSERT_EQUAL_UINT16(original.levelIndex(), restored.levelIndex());
  TEST_ASSERT_EQUAL_INT(original.playerRow(), restored.playerRow());
  TEST_ASSERT_EQUAL_INT(original.playerColumn(), restored.playerColumn());
  TEST_ASSERT_EQUAL_UINT16(original.moveCount(), restored.moveCount());
}

void test_sokoban_invalid_snapshot_is_rejected() {
  SokobanGame game;
  SokobanGame::Snapshot invalid = game.snapshot();
  invalid.levelIndex = SokobanGame::kLevelCount;
  TEST_ASSERT_FALSE(game.restore(invalid));
  invalid = game.snapshot();
  invalid.pushes = 2;
  invalid.moves = 1;
  TEST_ASSERT_FALSE(game.restore(invalid));
  invalid = game.snapshot();
  invalid.boxes[0] |= 1ULL;
  TEST_ASSERT_FALSE(game.restore(invalid));
}

void test_all_microban_sokoban_levels_are_structurally_valid() {
  for (uint16_t level = 0; level < SokobanGame::kLevelCount; ++level) {
    SokobanGame game;
    game.start(level);
    TEST_ASSERT_TRUE_MESSAGE(game.valid(), "Microban level failed to load");
    TEST_ASSERT_TRUE(game.width() > 0);
    TEST_ASSERT_TRUE(game.width() <= SokobanGame::kMaxWidth);
    TEST_ASSERT_TRUE(game.height() > 0);
    TEST_ASSERT_TRUE(game.height() <= SokobanGame::kMaxHeight);
    TEST_ASSERT_TRUE(game.isPlayable(game.playerRow(), game.playerColumn()));

    int boxes = 0;
    int targets = 0;
    for (int row = 0; row < game.height(); ++row) {
      for (int column = 0; column < game.width(); ++column) {
        if (game.hasBox(row, column)) ++boxes;
        if (game.isTarget(row, column)) ++targets;
      }
    }
    TEST_ASSERT_TRUE(boxes > 0);
    TEST_ASSERT_EQUAL_INT(boxes, targets);
  }

  SokobanGame largest;
  largest.start(SokobanGame::kLevelCount - 1);
  TEST_ASSERT_EQUAL_INT(30, largest.width());
  TEST_ASSERT_EQUAL_INT(17, largest.height());
  largest.nextLevel();
  TEST_ASSERT_EQUAL_UINT16(0, largest.levelIndex());
}

void test_long_game_progress_only_advances_with_valid_checkpoints() {
  game_progress::Advancement result =
      game_progress::evaluateAdvancement(7, 8, 155);
  TEST_ASSERT_TRUE(result.valid);
  TEST_ASSERT_TRUE(result.changed);
  TEST_ASSERT_EQUAL_UINT16(8, result.checkpoint);

  result = game_progress::evaluateAdvancement(8, 3, 155);
  TEST_ASSERT_TRUE(result.valid);
  TEST_ASSERT_FALSE(result.changed);
  TEST_ASSERT_EQUAL_UINT16(8, result.checkpoint);

  TEST_ASSERT_FALSE(
      game_progress::evaluateAdvancement(8, 155, 155).valid);
  TEST_ASSERT_FALSE(game_progress::evaluateAdvancement(0, 0, 0).valid);

  result = game_progress::evaluateAdvancement(154, 155, 156);
  TEST_ASSERT_TRUE(result.valid);
  TEST_ASSERT_TRUE(result.changed);
  TEST_ASSERT_EQUAL_UINT16(155, result.checkpoint);
}

void test_slitherlink_edges_cycle_blank_line_cross_blank() {
  SlitherlinkGame game;
  game.start(0);

  TEST_ASSERT_EQUAL(SlitherlinkGame::Blank, game.horizontalEdge(0, 0));
  TEST_ASSERT_TRUE(game.cycleHorizontal(0, 0));
  TEST_ASSERT_EQUAL(SlitherlinkGame::Line, game.horizontalEdge(0, 0));
  TEST_ASSERT_TRUE(game.cycleHorizontal(0, 0));
  TEST_ASSERT_EQUAL(SlitherlinkGame::Cross, game.horizontalEdge(0, 0));
  TEST_ASSERT_TRUE(game.cycleHorizontal(0, 0));
  TEST_ASSERT_EQUAL(SlitherlinkGame::Blank, game.horizontalEdge(0, 0));
}

void test_slitherlink_clue_counts_only_line_edges() {
  SlitherlinkGame game;
  game.start(0);
  TEST_ASSERT_EQUAL_INT8(1, game.clue(0, 1));

  TEST_ASSERT_TRUE(game.setHorizontalEdge(0, 1, SlitherlinkGame::Line));
  TEST_ASSERT_TRUE(game.setVerticalEdge(0, 1, SlitherlinkGame::Cross));
  TEST_ASSERT_EQUAL_UINT8(1, game.clueLineCount(0, 1));
  TEST_ASSERT_TRUE(game.clueSatisfied(0, 1));
}

void test_slitherlink_next_puzzle_clears_edges() {
  SlitherlinkGame game;
  game.start(0);
  TEST_ASSERT_TRUE(game.cycleVertical(0, 0));
  game.nextPuzzle();

  TEST_ASSERT_EQUAL_UINT8(1, game.puzzleIndex());
  TEST_ASSERT_EQUAL(SlitherlinkGame::Blank, game.verticalEdge(0, 0));
}

void test_slitherlink_invalid_snapshot_is_rejected() {
  SlitherlinkGame game;
  TEST_ASSERT_FALSE(game.restore({3ULL, 0, 0}));
  TEST_ASSERT_FALSE(game.restore({0, 0, SlitherlinkGame::kPuzzleCount}));
}

struct SlitherlinkTestEdge {
  uint8_t row;
  uint8_t column;
};

template <size_t HorizontalCount, size_t VerticalCount>
void solveSlitherlink(
    SlitherlinkGame& game,
    const SlitherlinkTestEdge (&horizontal)[HorizontalCount],
    const SlitherlinkTestEdge (&vertical)[VerticalCount]) {
  for (const SlitherlinkTestEdge& edge : horizontal) {
    TEST_ASSERT_TRUE(game.setHorizontalEdge(
        edge.row, edge.column, SlitherlinkGame::Line));
  }
  for (const SlitherlinkTestEdge& edge : vertical) {
    TEST_ASSERT_TRUE(
        game.setVerticalEdge(edge.row, edge.column, SlitherlinkGame::Line));
  }
}

void test_every_slitherlink_puzzle_has_a_verified_single_loop_solution() {
  static constexpr SlitherlinkTestEdge kHorizontal0[] = {
      {1, 1}, {1, 2}, {1, 3}, {4, 1}, {4, 2}, {4, 3},
  };
  static constexpr SlitherlinkTestEdge kVertical0[] = {
      {1, 1}, {1, 4}, {2, 1}, {2, 4}, {3, 1}, {3, 4},
  };
  static constexpr SlitherlinkTestEdge kHorizontal1[] = {
      {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4},
      {5, 0}, {5, 1}, {5, 2}, {5, 3}, {5, 4},
  };
  static constexpr SlitherlinkTestEdge kVertical1[] = {
      {0, 0}, {0, 5}, {1, 0}, {1, 5}, {2, 0},
      {2, 5}, {3, 0}, {3, 5}, {4, 0}, {4, 5},
  };
  static constexpr SlitherlinkTestEdge kHorizontal2[] = {
      {0, 1}, {0, 2}, {0, 3}, {2, 3}, {3, 0}, {4, 3},
      {4, 4}, {5, 0}, {5, 1}, {5, 2}, {5, 3}, {5, 4},
  };
  static constexpr SlitherlinkTestEdge kVertical2[] = {
      {0, 1}, {0, 4}, {1, 1}, {1, 4}, {2, 1},
      {2, 3}, {3, 0}, {3, 3}, {4, 0}, {4, 5},
  };
  static constexpr SlitherlinkTestEdge kHorizontal3[] = {
      {0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 0}, {3, 2},
      {3, 3}, {3, 4}, {5, 0}, {5, 1}, {5, 2}, {5, 3}, {5, 4},
  };
  static constexpr SlitherlinkTestEdge kVertical3[] = {
      {0, 1}, {0, 4}, {1, 1}, {1, 2}, {2, 0},
      {2, 2}, {3, 0}, {3, 5}, {4, 0}, {4, 5},
  };
  static constexpr SlitherlinkTestEdge kHorizontal4[] = {
      {0, 3}, {0, 4}, {1, 0}, {1, 1}, {1, 2}, {2, 0},
      {2, 1}, {2, 2}, {2, 3}, {4, 1}, {4, 2}, {4, 3},
      {5, 1}, {5, 2}, {5, 3}, {5, 4},
  };
  static constexpr SlitherlinkTestEdge kVertical4[] = {
      {0, 3}, {0, 5}, {1, 0}, {1, 5}, {2, 4},
      {2, 5}, {3, 4}, {3, 5}, {4, 1}, {4, 5},
  };

  SlitherlinkGame game;
  game.start(0);
  solveSlitherlink(game, kHorizontal0, kVertical0);
  TEST_ASSERT_TRUE(game.solved());
  game.start(1);
  solveSlitherlink(game, kHorizontal1, kVertical1);
  TEST_ASSERT_TRUE(game.solved());
  game.start(2);
  solveSlitherlink(game, kHorizontal2, kVertical2);
  TEST_ASSERT_TRUE(game.solved());
  game.start(3);
  solveSlitherlink(game, kHorizontal3, kVertical3);
  TEST_ASSERT_TRUE(game.solved());
  game.start(4);
  solveSlitherlink(game, kHorizontal4, kVertical4);
  TEST_ASSERT_TRUE(game.solved());
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
  RUN_TEST(test_2048_start_adds_two_tiles);
  RUN_TEST(test_2048_left_move_merges_each_pair_once);
  RUN_TEST(test_2048_right_move_preserves_direction);
  RUN_TEST(test_2048_up_move_merges_columns);
  RUN_TEST(test_2048_no_op_move_does_not_add_tile);
  RUN_TEST(test_2048_full_board_without_merges_is_game_over);
  RUN_TEST(test_2048_snapshot_restores_score_and_win);
  RUN_TEST(test_2048_invalid_snapshot_is_rejected);
  RUN_TEST(test_pipe_connect_generation_creates_scrambled_tree);
  RUN_TEST(test_pipe_connect_four_rotations_restore_tile);
  RUN_TEST(test_pipe_connect_solution_connects_every_tile);
  RUN_TEST(test_pipe_connect_reset_restores_scramble);
  RUN_TEST(test_pipe_connect_snapshot_restores_progress);
  RUN_TEST(test_pipe_connect_invalid_snapshot_is_rejected);
  RUN_TEST(test_pipe_connect_many_seeds_are_solvable_and_scrambled);
  RUN_TEST(test_minesweeper_first_reveal_is_safe_and_opens_area);
  RUN_TEST(test_minesweeper_first_reveal_excludes_neighbors);
  RUN_TEST(test_minesweeper_generates_six_mines);
  RUN_TEST(test_minesweeper_flag_blocks_reveal);
  RUN_TEST(test_minesweeper_revealing_mine_loses);
  RUN_TEST(test_minesweeper_complete_safe_board_wins);
  RUN_TEST(test_minesweeper_reset_keeps_board_and_clears_progress);
  RUN_TEST(test_minesweeper_snapshot_restores_progress);
  RUN_TEST(test_minesweeper_invalid_snapshot_is_rejected);
  RUN_TEST(test_minesweeper_revealed_number_opens_neighbors_when_fulfilled);
  RUN_TEST(test_minesweeper_revealed_number_waits_for_enough_flags);
  RUN_TEST(test_minesweeper_incorrect_fulfilled_flags_can_hit_mine);
  RUN_TEST(test_nonogram_cycles_blank_filled_crossed_blank);
  RUN_TEST(test_nonogram_matches_only_filled_solution_cells);
  RUN_TEST(test_nonogram_extra_filled_cell_prevents_completion);
  RUN_TEST(test_nonogram_generates_row_and_column_clues);
  RUN_TEST(test_nonogram_reset_clears_marks_but_keeps_solution);
  RUN_TEST(test_nonogram_snapshot_restores_progress);
  RUN_TEST(test_nonogram_invalid_snapshot_is_rejected);
  RUN_TEST(test_reversi_starts_with_four_discs_and_four_legal_moves);
  RUN_TEST(test_reversi_move_places_disc_and_flips_opponent);
  RUN_TEST(test_reversi_move_flips_in_all_eight_directions);
  RUN_TEST(test_reversi_rejects_non_capturing_and_occupied_moves);
  RUN_TEST(test_reversi_full_board_is_game_over);
  RUN_TEST(test_reversi_automatically_passes_when_opponent_has_no_move);
  RUN_TEST(test_reversi_ai_selects_a_legal_move);
  RUN_TEST(test_reversi_ai_prefers_an_available_corner);
  RUN_TEST(test_reversi_snapshot_restores_progress);
  RUN_TEST(test_reversi_invalid_snapshot_is_rejected);
  RUN_TEST(test_dots_and_boxes_starts_empty_with_player_one);
  RUN_TEST(test_dots_and_boxes_alternates_after_uncaptured_edge);
  RUN_TEST(test_dots_and_boxes_capture_grants_another_turn);
  RUN_TEST(test_dots_and_boxes_shared_edge_can_capture_two_boxes);
  RUN_TEST(test_dots_and_boxes_snapshot_rejects_unowned_complete_box);
  RUN_TEST(test_peg_solitaire_starts_with_center_empty);
  RUN_TEST(test_peg_solitaire_direct_jump_removes_middle_peg);
  RUN_TEST(test_peg_solitaire_taps_select_and_complete_jump);
  RUN_TEST(test_peg_solitaire_rejects_invalid_jump);
  RUN_TEST(test_peg_solitaire_one_center_peg_is_solved);
  RUN_TEST(test_peg_solitaire_invalid_snapshot_is_rejected);
  RUN_TEST(test_sokoban_first_microban_level_has_known_solution);
  RUN_TEST(test_sokoban_rejects_wall_and_resets_progress);
  RUN_TEST(test_sokoban_next_level_and_snapshot_restore);
  RUN_TEST(test_sokoban_invalid_snapshot_is_rejected);
  RUN_TEST(test_all_microban_sokoban_levels_are_structurally_valid);
  RUN_TEST(test_long_game_progress_only_advances_with_valid_checkpoints);
  RUN_TEST(test_slitherlink_edges_cycle_blank_line_cross_blank);
  RUN_TEST(test_slitherlink_clue_counts_only_line_edges);
  RUN_TEST(test_slitherlink_next_puzzle_clears_edges);
  RUN_TEST(test_slitherlink_invalid_snapshot_is_rejected);
  RUN_TEST(
      test_every_slitherlink_puzzle_has_a_verified_single_loop_solution);
  return UNITY_END();
}
