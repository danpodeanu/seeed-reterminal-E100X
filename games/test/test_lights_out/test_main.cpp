#include <unity.h>

#include <cstring>

#include "crossword_game.h"
#include "dots_and_boxes_game.h"
#include "double_tap_tracker.h"
#include "epub_browser_logic.h"
#include "epub_cover.h"
#include "epub_text.h"
#include "falling_blocks_game.h"
#include "game_2048.h"
#include "game_help_text.h"
#include "game_localization.h"
#include "game_progress_store.h"
#include "game_ranking.h"
#include "klondike_game.h"
#include "lights_out_game.h"
#include "mahjong_solitaire_game.h"
#include "menu_edge_swipe.h"
#include "mini_minesweeper_game.h"
#include "nonogram_game.h"
#include "ok_button_action.h"
#include "peg_solitaire_game.h"
#include "pipe_connect_game.h"
#include "reversi_game.h"
#include "sd_card_identity.h"
#include "slitherlink_game.h"
#include "sokoban_game.h"
#include "sudoku_game.h"

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

void test_2048_retains_loaded_best_score_across_new_games() {
  Game2048 game;
  game.retainBestScore(4096);
  game.retainBestScore(1024);
  game.start(0, 1);

  TEST_ASSERT_EQUAL_UINT32(4096, game.bestScore());
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

void test_high_score_progress_never_moves_backward() {
  game_progress::HighScoreAdvancement result =
      game_progress::evaluateHighScore(2048, 4096);
  TEST_ASSERT_TRUE(result.changed);
  TEST_ASSERT_EQUAL_UINT32(4096, result.score);

  result = game_progress::evaluateHighScore(4096, 2048);
  TEST_ASSERT_FALSE(result.changed);
  TEST_ASSERT_EQUAL_UINT32(4096, result.score);

  result = game_progress::evaluateHighScore(UINT32_MAX, UINT32_MAX);
  TEST_ASSERT_FALSE(result.changed);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, result.score);
}

void test_game_ranking_sorts_counts_and_preserves_ties() {
  const uint32_t counts[] = {2, 9, 9, 0, 4, 1, 0, 4, 2, 9, 0, 1};
  uint8_t ranking[12] = {};
  game_ranking::rankByPlayCount(counts, ranking);
  const uint8_t expected[] = {1, 2, 9, 4, 7, 0, 8, 5, 11, 3, 6, 10};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, ranking, 12);
}

void test_game_ranking_uses_default_order_only_to_break_ties() {
  const uint32_t counts[] = {4, 8, 4, 1, 8};
  const uint8_t defaultOrder[] = {4, 2, 0, 1, 3};
  uint8_t ranking[5] = {};
  game_ranking::rankByPlayCount(counts, defaultOrder, ranking);
  const uint8_t expected[] = {4, 1, 2, 0, 3};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, ranking, 5);
}

void test_game_play_count_saturates() {
  TEST_ASSERT_EQUAL_UINT32(1, game_ranking::nextPlayCount(0));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
                           game_ranking::nextPlayCount(UINT32_MAX - 1));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
                           game_ranking::nextPlayCount(UINT32_MAX));
}

void test_game_ranking_finds_current_launcher_page() {
  const uint8_t ranking[] = {4, 1, 7, 3, 5, 0, 2, 6};
  TEST_ASSERT_EQUAL_UINT(
      0, game_ranking::pageForGame(ranking, 7, 3));
  TEST_ASSERT_EQUAL_UINT(
      1, game_ranking::pageForGame(ranking, 0, 3));
  TEST_ASSERT_EQUAL_UINT(
      2, game_ranking::pageForGame(ranking, 6, 3));
}

void test_menu_edge_swipes_paginate_inward() {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(menu_edge_swipe::Direction::Previous),
      static_cast<int>(
          menu_edge_swipe::detect(10, 300, 100, 305, 480, 40, 45)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(menu_edge_swipe::Direction::Next),
      static_cast<int>(
          menu_edge_swipe::detect(470, 300, 380, 295, 480, 40, 45)));
}

void test_menu_edge_swipes_reject_non_paging_gestures() {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(menu_edge_swipe::Direction::None),
      static_cast<int>(
          menu_edge_swipe::detect(200, 300, 100, 300, 480, 40, 45)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(menu_edge_swipe::Direction::None),
      static_cast<int>(
          menu_edge_swipe::detect(10, 300, 35, 300, 480, 40, 45)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(menu_edge_swipe::Direction::None),
      static_cast<int>(
          menu_edge_swipe::detect(10, 300, 70, 390, 480, 40, 45)));
}

void test_every_game_translation_is_present() {
  for (size_t text = 0; text < game_localization::kTextCount; ++text) {
    for (size_t language = 0;
         language < game_localization::kLanguageCount; ++language) {
      const char* translated = game_localization::text(
          static_cast<game_localization::Language>(language),
          static_cast<game_localization::TextId>(text));
      TEST_ASSERT_NOT_NULL(translated);
      TEST_ASSERT_NOT_EQUAL('\0', translated[0]);
    }
  }
  TEST_ASSERT_EQUAL_STRING(
      u8"简体中文",
      game_localization::languageName(
          game_localization::Language::ChineseSimplified));
}

void test_every_game_help_translation_is_present() {
  for (size_t topic = 0; topic < game_help::kTopicCount; ++topic) {
    for (size_t language = 0;
         language < game_localization::kLanguageCount; ++language) {
      const char* instructions = game_help::text(
          static_cast<game_localization::Language>(language),
          static_cast<game_help::Topic>(topic));
      TEST_ASSERT_NOT_NULL(instructions);
      TEST_ASSERT_NOT_EQUAL('\0', instructions[0]);
      const size_t instructionLength = strlen(instructions);
      const epub_text::TextPage page = epub_text::paginate(
          instructions, instructionLength, 0, game_help::kColumnsPerLine,
          game_help::kMaximumLines);
      TEST_ASSERT_EQUAL_UINT(instructionLength, page.end);
    }
  }
}

void test_epub_browser_parent_folder_is_first_below_root() {
  TEST_ASSERT_FALSE(epub_browser_logic::hasParentFolder(""));
  TEST_ASSERT_FALSE(epub_browser_logic::hasParentFolder("/"));
  TEST_ASSERT_TRUE(epub_browser_logic::hasParentFolder("/Books"));

  TEST_ASSERT_EQUAL_INT(1, epub_browser_logic::itemCount(0, true));
  TEST_ASSERT_EQUAL_INT(4, epub_browser_logic::itemCount(3, true));
  TEST_ASSERT_TRUE(epub_browser_logic::isParentFolderItem(0, true));
  TEST_ASSERT_EQUAL_INT(-1, epub_browser_logic::storedEntryIndex(0, true));
  TEST_ASSERT_EQUAL_INT(0, epub_browser_logic::storedEntryIndex(1, true));
  TEST_ASSERT_FALSE(epub_browser_logic::isParentFolderItem(0, false));
  TEST_ASSERT_EQUAL_INT(0, epub_browser_logic::storedEntryIndex(0, false));
}

void test_ok_hold_duration_selects_requested_action() {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ok_button::Action::ShortPress),
      static_cast<int>(ok_button::actionForHold(0)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ok_button::Action::ShortPress),
      static_cast<int>(ok_button::actionForHold(1999)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ok_button::Action::DeepSleep),
      static_cast<int>(ok_button::actionForHold(2000)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ok_button::Action::DeepSleep),
      static_cast<int>(ok_button::actionForHold(5000)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ok_button::Action::DeepSleep),
      static_cast<int>(ok_button::actionForHold(5001)));
}

void test_every_sudoku_puzzle_has_a_valid_solution() {
  for (uint8_t puzzle = 0; puzzle < SudokuGame::kPuzzleCount; ++puzzle) {
    SudokuGame game;
    game.start(puzzle);
    int givens = 0;
    for (int row = 0; row < SudokuGame::kSize; ++row) {
      for (int column = 0; column < SudokuGame::kSize; ++column) {
        if (game.given(row, column)) {
          ++givens;
          continue;
        }
        TEST_ASSERT_TRUE(game.select(row, column));
        TEST_ASSERT_TRUE(game.setDigit(
            sudoku_puzzles::kPuzzles[puzzle].solution[row][column]));
      }
    }
    TEST_ASSERT_EQUAL_INT(38, givens);
    TEST_ASSERT_TRUE(game.solved());
  }
}

void test_sudoku_rejects_conflicts_and_restores_progress() {
  SudokuGame game;
  game.start(0);
  int editableRow = -1;
  int editableColumn = -1;
  for (int row = 0; row < SudokuGame::kSize && editableRow < 0; ++row) {
    for (int column = 0; column < SudokuGame::kSize; ++column) {
      if (!game.given(row, column)) {
        editableRow = row;
        editableColumn = column;
        break;
      }
    }
  }
  TEST_ASSERT_TRUE(game.select(editableRow, editableColumn));
  uint8_t conflict = 0;
  for (int column = 0; column < SudokuGame::kSize; ++column) {
    if (game.given(editableRow, column)) {
      conflict = game.at(editableRow, column);
      break;
    }
  }
  TEST_ASSERT_TRUE(conflict > 0);
  TEST_ASSERT_FALSE(game.setDigit(conflict));
  const uint8_t solution =
      sudoku_puzzles::kPuzzles[0].solution[editableRow][editableColumn];
  TEST_ASSERT_TRUE(game.setDigit(solution));

  SudokuGame restored;
  TEST_ASSERT_TRUE(restored.restore(game.snapshot()));
  TEST_ASSERT_EQUAL_UINT8(solution,
                          restored.at(editableRow, editableColumn));
  restored.reset();
  TEST_ASSERT_EQUAL_UINT8(0, restored.at(editableRow, editableColumn));
}

void test_every_crossword_puzzle_can_be_completed() {
  for (uint8_t puzzle = 0; puzzle < CrosswordGame::kPuzzleCount; ++puzzle) {
    CrosswordGame game;
    game.start(puzzle);
    TEST_ASSERT_TRUE(game.width() <= CrosswordGame::kMaxSize);
    TEST_ASSERT_TRUE(game.height() <= CrosswordGame::kMaxSize);
    for (int row = 0; row < game.height(); ++row) {
      for (int column = 0; column < game.width(); ++column) {
        if (game.blocked(row, column)) continue;
        TEST_ASSERT_TRUE(game.cellNumber(row, column) >= 0);
        TEST_ASSERT_TRUE(game.select(row, column));
        TEST_ASSERT_TRUE(game.setLetter(game.solutionAt(row, column)));
      }
    }
    TEST_ASSERT_TRUE(game.solved());
  }
}

void test_crossword_selection_toggles_direction_and_restores() {
  CrosswordGame game;
  game.start(0);
  TEST_ASSERT_TRUE(game.select(0, 0));
  const CrosswordGame::Direction firstDirection = game.direction();
  TEST_ASSERT_TRUE(game.select(0, 0));
  TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(firstDirection),
                        static_cast<uint8_t>(game.direction()));
  TEST_ASSERT_TRUE(game.currentClueNumber() > 0);
  TEST_ASSERT_TRUE(game.currentClueText()[0] != '\0');
  TEST_ASSERT_TRUE(game.setLetter('s'));

  CrosswordGame restored;
  TEST_ASSERT_TRUE(restored.restore(game.snapshot()));
  TEST_ASSERT_EQUAL_CHAR('S', restored.entryAt(0, 0));
}

void test_crossword_numbers_only_answer_starts() {
  CrosswordGame game;
  game.start(0);
  TEST_ASSERT_EQUAL_INT(1, game.cellNumber(0, 0));
  TEST_ASSERT_EQUAL_INT(0, game.cellNumber(0, 1));
  TEST_ASSERT_EQUAL_INT(2, game.cellNumber(0, 2));
  TEST_ASSERT_EQUAL_INT(3, game.cellNumber(0, 4));
  TEST_ASSERT_EQUAL_INT(0, game.cellNumber(1, 0));
  TEST_ASSERT_EQUAL_INT(4, game.cellNumber(2, 0));
  TEST_ASSERT_EQUAL_INT(5, game.cellNumber(2, 3));
  TEST_ASSERT_EQUAL_INT(6, game.cellNumber(3, 2));
  TEST_ASSERT_EQUAL_INT(7, game.cellNumber(4, 0));
}

void test_crossword_rejects_invalid_snapshot() {
  CrosswordGame game;
  CrosswordGame::Snapshot invalid = game.snapshot();
  invalid.puzzleIndex = CrosswordGame::kPuzzleCount;
  TEST_ASSERT_FALSE(game.restore(invalid));
  invalid = game.snapshot();
  invalid.entries[6] = 'A';
  TEST_ASSERT_FALSE(game.restore(invalid));
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

void test_klondike_deal_has_standard_pile_sizes_and_unique_cards() {
  KlondikeGame game;
  game.start(0x12345678UL);

  TEST_ASSERT_EQUAL_UINT8(24, game.stockCount());
  TEST_ASSERT_EQUAL_UINT8(0, game.wasteCount());
  bool seen[KlondikeGame::kCardCount] = {};
  int seenCount = 0;
  for (int column = 0; column < KlondikeGame::kTableauCount; ++column) {
    TEST_ASSERT_EQUAL_UINT8(column + 1, game.tableauCount(column));
    TEST_ASSERT_EQUAL_UINT8(column, game.faceUpStart(column));
    for (int index = 0; index < game.tableauCount(column); ++index) {
      const uint8_t card = game.tableauCard(column, index);
      TEST_ASSERT_LESS_THAN_UINT8(KlondikeGame::kCardCount, card);
      TEST_ASSERT_FALSE(seen[card]);
      seen[card] = true;
      ++seenCount;
    }
  }
  const KlondikeGame::Snapshot snapshot = game.snapshot();
  for (int index = 0; index < snapshot.stockCount; ++index) {
    const uint8_t card = snapshot.stock[index];
    TEST_ASSERT_LESS_THAN_UINT8(KlondikeGame::kCardCount, card);
    TEST_ASSERT_FALSE(seen[card]);
    seen[card] = true;
    ++seenCount;
  }
  TEST_ASSERT_EQUAL_INT(KlondikeGame::kCardCount, seenCount);
}

void test_klondike_stock_recycles_in_draw_order() {
  KlondikeGame game;
  game.start(17);
  uint8_t firstDraw = KlondikeGame::kNoCard;
  for (int draw = 0; draw < 24; ++draw) {
    TEST_ASSERT_EQUAL(KlondikeGame::StockResult::Drawn, game.drawStock());
    if (draw == 0) firstDraw = game.wasteTop();
  }
  TEST_ASSERT_EQUAL_UINT8(0, game.stockCount());
  TEST_ASSERT_EQUAL_UINT8(24, game.wasteCount());
  TEST_ASSERT_EQUAL(KlondikeGame::StockResult::Recycled, game.drawStock());
  TEST_ASSERT_EQUAL(KlondikeGame::StockResult::Drawn, game.drawStock());
  TEST_ASSERT_EQUAL_UINT8(firstDraw, game.wasteTop());
}

void test_klondike_moves_ace_from_waste_to_foundation() {
  bool movedAce = false;
  for (uint32_t seed = 1; seed <= 100 && !movedAce; ++seed) {
    KlondikeGame game;
    game.start(seed);
    for (int draw = 0; draw < 24; ++draw) {
      TEST_ASSERT_EQUAL(KlondikeGame::StockResult::Drawn, game.drawStock());
      const uint8_t card = game.wasteTop();
      if (KlondikeGame::rank(card) != 0) continue;
      const int suit = KlondikeGame::cardSuit(card);
      TEST_ASSERT_TRUE(game.selectWaste());
      TEST_ASSERT_TRUE(game.moveSelectionToMatchingFoundation());
      TEST_ASSERT_EQUAL_UINT8(1, game.foundationCount(suit));
      movedAce = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(movedAce);
}

void test_double_tap_tracker_requires_same_target_within_window() {
  DoubleTapTracker tracker;
  TEST_ASSERT_FALSE(tracker.registerTap(10, 1000, 500));
  TEST_ASSERT_FALSE(tracker.registerTap(11, 1200, 500));
  TEST_ASSERT_TRUE(tracker.registerTap(11, 1700, 500));
  TEST_ASSERT_FALSE(tracker.registerTap(11, 1800, 500));
  TEST_ASSERT_FALSE(tracker.registerTap(11, 2301, 500));
  TEST_ASSERT_TRUE(tracker.registerTap(11, 2400, 500));
}

void test_klondike_moves_legal_tableau_card_and_restores_snapshot() {
  bool movedCard = false;
  for (uint32_t seed = 1; seed <= 200 && !movedCard; ++seed) {
    KlondikeGame game;
    game.start(seed);
    for (int source = 0; source < KlondikeGame::kTableauCount && !movedCard;
         ++source) {
      const int sourceIndex = game.tableauCount(source) - 1;
      const uint8_t moving = game.tableauCard(source, sourceIndex);
      for (int destination = 0;
           destination < KlondikeGame::kTableauCount; ++destination) {
        if (source == destination) continue;
        const int destinationIndex = game.tableauCount(destination) - 1;
        const uint8_t target =
            game.tableauCard(destination, destinationIndex);
        if (!KlondikeGame::canStack(target, moving)) continue;
        const uint8_t previousDestinationCount =
            game.tableauCount(destination);
        TEST_ASSERT_TRUE(game.selectTableau(source, sourceIndex));
        TEST_ASSERT_TRUE(game.moveSelectionToTableau(destination));
        TEST_ASSERT_EQUAL_UINT8(previousDestinationCount + 1,
                                game.tableauCount(destination));

        const KlondikeGame::Snapshot snapshot = game.snapshot();
        KlondikeGame restored;
        TEST_ASSERT_TRUE(restored.restore(snapshot));
        TEST_ASSERT_EQUAL_UINT16(game.moves(), restored.moves());
        TEST_ASSERT_EQUAL_UINT8(game.tableauCount(destination),
                                restored.tableauCount(destination));
        TEST_ASSERT_EQUAL_UINT8(
            game.tableauCard(destination, game.tableauCount(destination) - 1),
            restored.tableauCard(destination,
                                 restored.tableauCount(destination) - 1));
        movedCard = true;
        break;
      }
    }
  }
  TEST_ASSERT_TRUE(movedCard);
}

void test_klondike_rejects_duplicate_card_snapshot() {
  KlondikeGame game;
  KlondikeGame::Snapshot invalid = game.snapshot();
  invalid.stock[0] = invalid.stock[1];
  TEST_ASSERT_FALSE(game.restore(invalid));
}

void test_mahjong_layout_has_144_tiles_and_four_of_each_type() {
  MahjongSolitaireGame game;
  game.start(0xC0FFEEUL);

  TEST_ASSERT_EQUAL_INT(144, game.remaining());
  TEST_ASSERT_TRUE(game.hasMoves());
  uint8_t counts[MahjongSolitaireGame::kTileTypeCount] = {};
  for (int index = 0; index < MahjongSolitaireGame::kTileCount; ++index) {
    ++counts[game.tileType(index)];
  }
  for (int type = 0; type < MahjongSolitaireGame::kTileTypeCount; ++type) {
    TEST_ASSERT_EQUAL_UINT8(4, counts[type]);
  }
  TEST_ASSERT_FALSE(game.free(41));
}

void test_mahjong_known_pair_sequence_solves_every_deal() {
  MahjongSolitaireGame game;
  game.start(99);
  const int starts[] = {0, 96, 128, 140};
  const int widths[] = {12, 8, 4, 2};
  const int rows[] = {8, 4, 3, 2};

  for (int layer = 3; layer >= 0; --layer) {
    for (int row = 0; row < rows[layer]; ++row) {
      for (int left = 0; left < widths[layer] / 2; ++left) {
        const int first = starts[layer] + row * widths[layer] + left;
        const int second =
            starts[layer] + row * widths[layer] + widths[layer] - left - 1;
        TEST_ASSERT_TRUE(game.free(first));
        TEST_ASSERT_TRUE(game.free(second));
        TEST_ASSERT_EQUAL_UINT8(game.tileType(first), game.tileType(second));
        TEST_ASSERT_EQUAL(MahjongSolitaireGame::TapResult::Selected,
                          game.tap(first));
        const MahjongSolitaireGame::TapResult result = game.tap(second);
        TEST_ASSERT_TRUE(result == MahjongSolitaireGame::TapResult::Removed ||
                         result == MahjongSolitaireGame::TapResult::Won);
      }
    }
  }
  TEST_ASSERT_TRUE(game.solved());
  TEST_ASSERT_EQUAL_UINT16(72, game.moves());
}

void test_mahjong_snapshot_restores_selection_and_removed_pair() {
  MahjongSolitaireGame game;
  game.start(123);
  TEST_ASSERT_EQUAL(MahjongSolitaireGame::TapResult::Selected, game.tap(140));
  TEST_ASSERT_EQUAL(MahjongSolitaireGame::TapResult::Removed, game.tap(141));
  TEST_ASSERT_EQUAL(MahjongSolitaireGame::TapResult::Selected, game.tap(142));

  const MahjongSolitaireGame::Snapshot snapshot = game.snapshot();
  MahjongSolitaireGame restored;
  TEST_ASSERT_TRUE(restored.restore(snapshot));
  TEST_ASSERT_EQUAL_INT(142, restored.selectedIndex());
  TEST_ASSERT_EQUAL_INT(142, restored.remaining());
  TEST_ASSERT_FALSE(restored.occupied(140));
  TEST_ASSERT_FALSE(restored.occupied(141));
}

void test_mahjong_rejects_inconsistent_snapshot() {
  MahjongSolitaireGame game;
  MahjongSolitaireGame::Snapshot invalid = game.snapshot();
  invalid.occupied[0] &= ~1ULL;
  TEST_ASSERT_FALSE(game.restore(invalid));
}

void test_falling_blocks_starts_with_one_active_piece() {
  FallingBlocksGame game;
  game.start(1);

  int activeCells = 0;
  int settledCells = 0;
  for (int row = 0; row < FallingBlocksGame::kHeight; ++row) {
    for (int column = 0; column < FallingBlocksGame::kWidth; ++column) {
      if (game.at(row, column) != 0) ++activeCells;
      if (game.settledAt(row, column) != 0) ++settledCells;
    }
  }
  TEST_ASSERT_EQUAL_INT(4, activeCells);
  TEST_ASSERT_EQUAL_INT(0, settledCells);
  TEST_ASSERT_FALSE(game.gameOver());
}

void test_falling_blocks_turn_moves_and_advances_gravity() {
  FallingBlocksGame game;
  game.start(2);
  const int initialColumn = game.activeColumn();

  TEST_ASSERT_TRUE(game.turn(FallingBlocksGame::Action::MoveLeft));
  TEST_ASSERT_EQUAL_INT(initialColumn - 1, game.activeColumn());
  TEST_ASSERT_EQUAL_INT(1, game.activeRow());
  TEST_ASSERT_TRUE(game.turn(FallingBlocksGame::Action::RotateClockwise));
  TEST_ASSERT_EQUAL_UINT8(1, game.rotation());
  TEST_ASSERT_EQUAL_INT(2, game.activeRow());
}

void test_falling_blocks_rotates_in_both_directions() {
  FallingBlocksGame game;
  game.start(2);

  TEST_ASSERT_TRUE(
      game.turn(FallingBlocksGame::Action::RotateCounterclockwise));
  TEST_ASSERT_EQUAL_UINT8(3, game.rotation());
  TEST_ASSERT_EQUAL_INT(1, game.activeRow());
  TEST_ASSERT_TRUE(game.turn(FallingBlocksGame::Action::RotateClockwise));
  TEST_ASSERT_EQUAL_UINT8(0, game.rotation());
  TEST_ASSERT_EQUAL_INT(2, game.activeRow());
}

void test_falling_blocks_hard_drop_locks_piece_and_scores() {
  FallingBlocksGame game;
  game.start(3);
  const uint8_t nextPiece = game.nextPiece();

  TEST_ASSERT_TRUE(game.turn(FallingBlocksGame::Action::HardDrop));
  int settledCells = 0;
  for (int row = 0; row < FallingBlocksGame::kHeight; ++row) {
    for (int column = 0; column < FallingBlocksGame::kWidth; ++column) {
      if (game.settledAt(row, column) != 0) ++settledCells;
    }
  }
  TEST_ASSERT_EQUAL_INT(4, settledCells);
  TEST_ASSERT_EQUAL_UINT8(nextPiece, game.activePiece());
  TEST_ASSERT_GREATER_THAN_UINT32(0, game.score());
}

void test_falling_blocks_clears_completed_line() {
  FallingBlocksGame game;
  game.start(4);
  FallingBlocksGame::Snapshot snapshot = game.snapshot();
  std::memset(snapshot.cells, 0, sizeof(snapshot.cells));
  for (int column = 0; column < FallingBlocksGame::kWidth; ++column) {
    if (column < 3 || column > 6) {
      snapshot.cells[(FallingBlocksGame::kHeight - 1) *
                         FallingBlocksGame::kWidth +
                     column] = 2;
    }
  }
  snapshot.activePiece = 0;
  snapshot.rotation = 0;
  snapshot.activeRow = FallingBlocksGame::kHeight - 1;
  snapshot.activeColumn = 3;
  snapshot.gameOver = 0;
  TEST_ASSERT_TRUE(game.restore(snapshot));

  TEST_ASSERT_TRUE(game.turn(FallingBlocksGame::Action::SoftDrop));
  TEST_ASSERT_EQUAL_UINT16(1, game.lines());
  TEST_ASSERT_EQUAL_UINT32(100, game.score());
  for (int column = 0; column < FallingBlocksGame::kWidth; ++column) {
    TEST_ASSERT_EQUAL_UINT8(
        0, game.settledAt(FallingBlocksGame::kHeight - 1, column));
  }
}

void test_falling_blocks_snapshot_restores_active_game() {
  FallingBlocksGame game;
  game.start(55);
  game.turn(FallingBlocksGame::Action::MoveRight);
  game.turn(FallingBlocksGame::Action::RotateClockwise);
  const FallingBlocksGame::Snapshot snapshot = game.snapshot();

  FallingBlocksGame restored;
  TEST_ASSERT_TRUE(restored.restore(snapshot));
  TEST_ASSERT_EQUAL_UINT8(game.activePiece(), restored.activePiece());
  TEST_ASSERT_EQUAL_UINT8(game.nextPiece(), restored.nextPiece());
  TEST_ASSERT_EQUAL_UINT8(game.rotation(), restored.rotation());
  TEST_ASSERT_EQUAL_INT(game.activeRow(), restored.activeRow());
  TEST_ASSERT_EQUAL_INT(game.activeColumn(), restored.activeColumn());
  TEST_ASSERT_EQUAL_UINT32(game.score(), restored.score());
}

void test_falling_blocks_rejects_invalid_snapshot() {
  FallingBlocksGame game;
  FallingBlocksGame::Snapshot invalid = game.snapshot();
  invalid.cells[0] = FallingBlocksGame::kPieceCount + 1;
  TEST_ASSERT_FALSE(game.restore(invalid));

  invalid = game.snapshot();
  invalid.activePiece = FallingBlocksGame::kNoPiece;
  TEST_ASSERT_FALSE(game.restore(invalid));
}

void test_falling_blocks_eventually_ends_when_stack_reaches_top() {
  FallingBlocksGame game;
  game.start(77);
  for (int piece = 0; piece < 100 && !game.gameOver(); ++piece) {
    TEST_ASSERT_TRUE(game.turn(FallingBlocksGame::Action::HardDrop));
  }
  TEST_ASSERT_TRUE(game.gameOver());
  TEST_ASSERT_FALSE(game.turn(FallingBlocksGame::Action::SoftDrop));
}

void test_epub_html_to_text_preserves_blocks_and_decodes_entities() {
  const char html[] =
      "<html><head><style>hidden</style></head><body><h1>One &amp; "
      "Two</h1><p>Hello&nbsp;world.<br/>Next line.</p>"
      "<script>ignored()</script><p>&#x00E9;lan</p></body></html>";
  const std::string text =
      epub_text::htmlToPlainText(html, sizeof(html) - 1);
  TEST_ASSERT_EQUAL_STRING("One & Two\n\nHello world.\nNext line.\n\nélan",
                           text.c_str());
}

void test_epub_html_to_text_can_reuse_the_extraction_buffer() {
  char html[] =
      "<body><p>First&nbsp;line</p><script>drop me</script>"
      "<p>&#x7532;&#20057;</p></body>";
  const size_t length = epub_text::htmlToPlainTextInPlace(
      html, sizeof(html) - 1);
  TEST_ASSERT_EQUAL_STRING("First line\n\n甲乙", html);
  TEST_ASSERT_EQUAL_UINT32(sizeof("First line\n\n甲乙") - 1, length);
  TEST_ASSERT_LESS_THAN_UINT32(sizeof(html) - 1, length);
}

void test_epub_html_to_text_separates_chapter_intro_from_body() {
  char html[] =
      "<body><div class='chapter-number'>Chapter 1</div>"
      "<h1>Arrival</h1><p>The story begins.</p></body>";
  epub_text::htmlToPlainTextInPlace(html, sizeof(html) - 1);
  TEST_ASSERT_EQUAL_STRING("Chapter 1\n\nArrival\n\nThe story begins.",
                           html);
}

void test_epub_html_to_text_preserves_emphasis_when_requested() {
  char html[] =
      "<body><h2>Heading</h2><p>Plain <strong>bold "
      "<em>both</em></strong> and <i>italic</i>.</p></body>";
  const size_t length = epub_text::htmlToPlainTextInPlace(
      html, sizeof(html) - 1, sizeof(html) - 1, true);
  std::string expected;
  expected += epub_text::styleMarker(epub_text::TextStyle::Bold);
  expected += "Heading\n\n";
  expected += epub_text::styleMarker(epub_text::TextStyle::Regular);
  expected += "Plain ";
  expected += epub_text::styleMarker(epub_text::TextStyle::Bold);
  expected += "bold ";
  expected += epub_text::styleMarker(epub_text::TextStyle::BoldItalic);
  expected += "both";
  expected += " ";
  expected += epub_text::styleMarker(epub_text::TextStyle::Regular);
  expected += "and ";
  expected += epub_text::styleMarker(epub_text::TextStyle::Italic);
  expected += "italic";
  expected += epub_text::styleMarker(epub_text::TextStyle::Regular);
  expected += ".";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), html);
  TEST_ASSERT_EQUAL_UINT32(expected.length(), length);
}

void test_epub_typography_normalization_keeps_style_markers() {
  std::string text;
  text += epub_text::styleMarker(epub_text::TextStyle::Italic);
  text += u8"“It’s”—fine…";
  text.resize(epub_text::normalizeTypographyInPlace(&text[0], text.length()));
  std::string expected;
  expected += epub_text::styleMarker(epub_text::TextStyle::Italic);
  expected += "\"It's\"-fine...";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), text.c_str());
}

void test_epub_xml_helpers_parse_attributes_and_resolve_paths() {
  const char tag[] =
      "rootfile media-type=\"application/oebps-package+xml\" "
      "full-path='OPS/content.opf'";
  TEST_ASSERT_EQUAL_STRING(
      "OPS/content.opf",
      epub_text::attribute(tag, sizeof(tag) - 1, "full-path").c_str());
  const char firstAttribute[] = "rootfile full-path=\"OPS/package.opf\"";
  TEST_ASSERT_EQUAL_STRING(
      "OPS/package.opf",
      epub_text::attribute(firstAttribute, sizeof(firstAttribute) - 1,
                           "full-path")
          .c_str());
  TEST_ASSERT_EQUAL_STRING(
      "OPS/Text/chapter.xhtml",
      epub_text::resolvePath("OPS/content.opf", "Text/chapter.xhtml#part")
          .c_str());
  TEST_ASSERT_EQUAL_STRING(
      "Images/cover.jpg",
      epub_text::resolvePath("OPS/Text/chapter.xhtml", "../../Images/cover.jpg")
          .c_str());
  TEST_ASSERT_EQUAL_STRING(
      "OPS/Text/Chapter One.xhtml",
      epub_text::resolvePath("OPS/content.opf", "Text/Chapter%20One.xhtml")
          .c_str());
  const std::string container =
      "<rootfiles><rootfile full-path=\"OPS/content.opf\"/></rootfiles>";
  TEST_ASSERT_EQUAL_UINT32(
      container.find("<rootfile "),
      epub_text::findStartTag(container, "rootfile"));
}

void test_epub3_cover_uses_cover_image_manifest_property() {
  const std::string package =
      "<package><manifest>"
      "<item id=\"front\" href=\"Images/front.jpg\" "
      "media-type=\"image/jpeg\" properties=\"nav cover-image\"/>"
      "<item id=\"chapter\" href=\"Text/one.xhtml\" "
      "media-type=\"application/xhtml+xml\"/>"
      "</manifest></package>";

  TEST_ASSERT_EQUAL_STRING(
      "OPS/Images/front.jpg",
      epub_cover::findCoverPath(package, "OPS/content.opf").c_str());
}

void test_epub2_cover_uses_metadata_manifest_id() {
  const std::string package =
      "<package><metadata><meta name=\"cover\" content=\"cover-art\"/>"
      "</metadata><manifest>"
      "<item id=\"cover-art\" href=\"cover.png\" media-type=\"image/png\"/>"
      "</manifest></package>";

  TEST_ASSERT_EQUAL_STRING(
      "OEBPS/cover.png",
      epub_cover::findCoverPath(package, "OEBPS/book.opf").c_str());
}

void test_epub_cover_rejects_unsupported_image_formats() {
  const std::string package =
      "<package><manifest>"
      "<item id=\"front\" href=\"cover.svg\" media-type=\"image/svg+xml\" "
      "properties=\"cover-image\"/>"
      "</manifest></package>";

  TEST_ASSERT_TRUE(
      epub_cover::findCoverPath(package, "OPS/content.opf").empty());
}

void test_epub_folder_cover_uses_book_directory() {
  TEST_ASSERT_EQUAL_STRING(
      "/Books/cover.png",
      epub_cover::folderCoverPath("/Books/title.epub", "cover.png").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "/cover.jpg",
      epub_cover::folderCoverPath("/title.epub", "cover.jpg").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "cover.png",
      epub_cover::folderCoverPath("title.epub", "cover.png").c_str());
}

void test_sd_card_identity_uses_partition_boot_sector() {
  uint8_t sectorZero[512] = {};
  sectorZero[510] = 0x55;
  sectorZero[511] = 0xAA;
  sectorZero[446 + 4] = 0x0C;
  sectorZero[446 + 8] = 0x00;
  sectorZero[446 + 9] = 0x08;
  sectorZero[446 + 12] = 0x00;
  sectorZero[446 + 13] = 0x10;
  uint8_t volumeBoot[512] = {};
  volumeBoot[67] = 0x12;
  volumeBoot[68] = 0x34;

  TEST_ASSERT_EQUAL_UINT32(
      2048,
      sd_card_identity::partitionStartSector(sectorZero, sizeof(sectorZero)));
  const sd_card_identity::Identity first =
      sd_card_identity::identify(8192, sectorZero, sizeof(sectorZero),
                                 volumeBoot);
  const sd_card_identity::Identity same =
      sd_card_identity::identify(8192, sectorZero, sizeof(sectorZero),
                                 volumeBoot);
  TEST_ASSERT_TRUE(sd_card_identity::same(first, same));

  volumeBoot[68] ^= 0x01;
  const sd_card_identity::Identity changed =
      sd_card_identity::identify(8192, sectorZero, sizeof(sectorZero),
                                 volumeBoot);
  TEST_ASSERT_FALSE(sd_card_identity::same(first, changed));
}

void test_epub_pagination_wraps_utf8_without_splitting_characters() {
  const std::string text = "one two three\nélan four five";
  const epub_text::TextPage first =
      epub_text::paginate(text.data(), text.size(), 0, 7, 2);
  TEST_ASSERT_EQUAL_UINT32(2, first.lines.size());
  TEST_ASSERT_EQUAL_STRING("one two", first.lines[0].c_str());
  TEST_ASSERT_EQUAL_STRING("three", first.lines[1].c_str());
  TEST_ASSERT_TRUE(first.justifyLines[0]);
  TEST_ASSERT_FALSE(first.justifyLines[1]);

  const epub_text::TextPage second = epub_text::paginate(
      text.data(), text.size(), first.end, 7, 2);
  TEST_ASSERT_EQUAL_UINT32(2, second.lines.size());
  TEST_ASSERT_EQUAL_STRING("élan", second.lines[0].c_str());
  TEST_ASSERT_EQUAL_STRING("four", second.lines[1].c_str());
  TEST_ASSERT_FALSE(second.justifyLines[0]);
  TEST_ASSERT_FALSE(second.justifyLines[1]);
  TEST_ASSERT_EQUAL_UINT32(
      0, epub_text::previousPageStart(text.data(), text.size(), second.start, 7,
                                     2));
}

void test_epub_pagination_treats_cjk_as_full_width() {
  const std::string text = u8"甲乙丙丁戊";
  const epub_text::TextPage page =
      epub_text::paginate(text.data(), text.size(), 0, 4, 3);
  TEST_ASSERT_EQUAL_UINT32(3, page.lines.size());
  TEST_ASSERT_EQUAL_STRING(u8"甲乙", page.lines[0].c_str());
  TEST_ASSERT_EQUAL_STRING(u8"丙丁", page.lines[1].c_str());
  TEST_ASSERT_EQUAL_STRING(u8"戊", page.lines[2].c_str());
  TEST_ASSERT_TRUE(epub_text::containsCjk(text.data(), text.size()));
  const std::string korean = u8"한글";
  TEST_ASSERT_TRUE(epub_text::containsCjk(korean.data(), korean.size()));
  const std::string halfwidthKatakana = u8"\uFF76\uFF80\uFF76\uFF85";
  TEST_ASSERT_TRUE(epub_text::containsCjk(halfwidthKatakana.data(),
                                         halfwidthKatakana.size()));
  const std::string halfwidthHangul = u8"\uFFA1\uFFB2";
  TEST_ASSERT_TRUE(epub_text::containsCjk(halfwidthHangul.data(),
                                         halfwidthHangul.size()));
  TEST_ASSERT_EQUAL_UINT32(1, epub_text::displayColumns(0xFF76));
  TEST_ASSERT_FALSE(epub_text::containsCjk("Café", 5));
}

void test_epub_pagination_carries_style_between_pages() {
  std::string text;
  text += epub_text::styleMarker(epub_text::TextStyle::Bold);
  text += "one two three ";
  text += epub_text::styleMarker(epub_text::TextStyle::Italic);
  text += "four five";

  const epub_text::TextPage first =
      epub_text::paginate(text.data(), text.size(), 0, 7, 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub_text::TextStyle::Bold),
                        static_cast<int>(first.finalStyle));
  const epub_text::TextPage second = epub_text::paginate(
      text.data(), text.size(), first.end, 7, 1, first.finalStyle, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub_text::TextStyle::Bold),
                        static_cast<int>(second.initialStyle));
  const epub_text::TextPage third = epub_text::paginate(
      text.data(), text.size(), second.end, 7, 1, second.finalStyle, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(epub_text::TextStyle::Italic),
                        static_cast<int>(third.finalStyle));
  TEST_ASSERT_EQUAL_UINT32(
      second.start,
      epub_text::previousPageStart(text.data(), text.size(), third.start, 7, 1));
}

size_t test_epub_proportional_width(uint32_t codepoint,
                                    epub_text::TextStyle) {
  return codepoint == 'W' ? 26 : 10;
}

void test_epub_pagination_accepts_proportional_widths() {
  const std::string text = "WWWW normal";
  const epub_text::TextPage page = epub_text::paginate(
      text.data(), text.size(), 0, 80, 2, epub_text::TextStyle::Regular, true,
      test_epub_proportional_width);
  TEST_ASSERT_EQUAL_UINT32(2, page.lines.size());
  TEST_ASSERT_EQUAL_STRING("WWW", page.lines[0].c_str());
  TEST_ASSERT_FALSE(page.justifyLines[0]);
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
  RUN_TEST(test_2048_retains_loaded_best_score_across_new_games);
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
  RUN_TEST(test_high_score_progress_never_moves_backward);
  RUN_TEST(test_game_ranking_sorts_counts_and_preserves_ties);
  RUN_TEST(test_game_ranking_uses_default_order_only_to_break_ties);
  RUN_TEST(test_game_play_count_saturates);
  RUN_TEST(test_game_ranking_finds_current_launcher_page);
  RUN_TEST(test_menu_edge_swipes_paginate_inward);
  RUN_TEST(test_menu_edge_swipes_reject_non_paging_gestures);
  RUN_TEST(test_every_game_translation_is_present);
  RUN_TEST(test_every_game_help_translation_is_present);
  RUN_TEST(test_epub_browser_parent_folder_is_first_below_root);
  RUN_TEST(test_ok_hold_duration_selects_requested_action);
  RUN_TEST(test_every_sudoku_puzzle_has_a_valid_solution);
  RUN_TEST(test_sudoku_rejects_conflicts_and_restores_progress);
  RUN_TEST(test_every_crossword_puzzle_can_be_completed);
  RUN_TEST(test_crossword_selection_toggles_direction_and_restores);
  RUN_TEST(test_crossword_numbers_only_answer_starts);
  RUN_TEST(test_crossword_rejects_invalid_snapshot);
  RUN_TEST(test_slitherlink_edges_cycle_blank_line_cross_blank);
  RUN_TEST(test_slitherlink_clue_counts_only_line_edges);
  RUN_TEST(test_slitherlink_next_puzzle_clears_edges);
  RUN_TEST(test_slitherlink_invalid_snapshot_is_rejected);
  RUN_TEST(
      test_every_slitherlink_puzzle_has_a_verified_single_loop_solution);
  RUN_TEST(test_klondike_deal_has_standard_pile_sizes_and_unique_cards);
  RUN_TEST(test_klondike_stock_recycles_in_draw_order);
  RUN_TEST(test_klondike_moves_ace_from_waste_to_foundation);
  RUN_TEST(test_double_tap_tracker_requires_same_target_within_window);
  RUN_TEST(test_klondike_moves_legal_tableau_card_and_restores_snapshot);
  RUN_TEST(test_klondike_rejects_duplicate_card_snapshot);
  RUN_TEST(test_mahjong_layout_has_144_tiles_and_four_of_each_type);
  RUN_TEST(test_mahjong_known_pair_sequence_solves_every_deal);
  RUN_TEST(test_mahjong_snapshot_restores_selection_and_removed_pair);
  RUN_TEST(test_mahjong_rejects_inconsistent_snapshot);
  RUN_TEST(test_falling_blocks_starts_with_one_active_piece);
  RUN_TEST(test_falling_blocks_turn_moves_and_advances_gravity);
  RUN_TEST(test_falling_blocks_rotates_in_both_directions);
  RUN_TEST(test_falling_blocks_hard_drop_locks_piece_and_scores);
  RUN_TEST(test_falling_blocks_clears_completed_line);
  RUN_TEST(test_falling_blocks_snapshot_restores_active_game);
  RUN_TEST(test_falling_blocks_rejects_invalid_snapshot);
  RUN_TEST(test_falling_blocks_eventually_ends_when_stack_reaches_top);
  RUN_TEST(test_epub_html_to_text_preserves_blocks_and_decodes_entities);
  RUN_TEST(test_epub_html_to_text_can_reuse_the_extraction_buffer);
  RUN_TEST(test_epub_html_to_text_separates_chapter_intro_from_body);
  RUN_TEST(test_epub_html_to_text_preserves_emphasis_when_requested);
  RUN_TEST(test_epub_typography_normalization_keeps_style_markers);
  RUN_TEST(test_epub_xml_helpers_parse_attributes_and_resolve_paths);
  RUN_TEST(test_epub3_cover_uses_cover_image_manifest_property);
  RUN_TEST(test_epub2_cover_uses_metadata_manifest_id);
  RUN_TEST(test_epub_cover_rejects_unsupported_image_formats);
  RUN_TEST(test_epub_folder_cover_uses_book_directory);
  RUN_TEST(test_sd_card_identity_uses_partition_boot_sector);
  RUN_TEST(test_epub_pagination_wraps_utf8_without_splitting_characters);
  RUN_TEST(test_epub_pagination_treats_cjk_as_full_width);
  RUN_TEST(test_epub_pagination_carries_style_between_pages);
  RUN_TEST(test_epub_pagination_accepts_proportional_widths);
  return UNITY_END();
}
