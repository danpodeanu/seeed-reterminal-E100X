// Selected by the PlatformIO environment. Seeed_GFX reads this header while
// compiling the library, so each model receives its own panel driver.
#ifndef RETERMINAL_MODEL
#define RETERMINAL_MODEL 1001
#endif

#if RETERMINAL_MODEL == 1001
#define BOARD_SCREEN_COMBO 520  // UC8179, 800x480, Gray4
#elif RETERMINAL_MODEL == 1002
#define BOARD_SCREEN_COMBO 521  // ED2208, 800x480, six-color
#elif RETERMINAL_MODEL == 1003
#define BOARD_SCREEN_COMBO 522  // ED103TC2, 1872x1404, Gray16
#elif RETERMINAL_MODEL == 1004
#define BOARD_SCREEN_COMBO 523  // T133A01, 1200x1600, six-color
#else
#error "RETERMINAL_MODEL must be 1001, 1002, 1003, or 1004"
#endif
