#pragma once

#include <stdint.h>

// Generated from the MIT-licensed cout/minicross word database by
// tools/generate_crossword_header.py. Text clues are original to this project.
namespace crossword_puzzles {

struct Puzzle {
  uint16_t solutionOffset;
  uint16_t clueOffset;
  uint8_t width;
  uint8_t height;
  uint8_t clueCount;
};

struct Clue {
  uint16_t textOffset;
  uint8_t startIndex;
  uint8_t length;
  uint8_t direction;
};

inline constexpr char kSolutions[] =
    "SWEEPA#N#AWATER##EATBERRYCLUBS###E#STEAKSWAN#HORSEB#SSHELK#MA#AIMNOT##SHELLS#SSHLUCK#E#AIMEAR#IP#FAXSTARSO##UPCRANEK#X#ASTEAKMAP#WAIR#HSMILEK#Z#ASWEETSWEETOH#C#BATHE#LIONBEE#DDROPSR##I#ENTERSAW#USPOONA#LOGICE#RRAM#A#SOAPPEN#HSPADEMIC#AENTERL###TL#SSHSOCKSA#AIMWASTE#REEL#M##LMASKSA#P#LPLATE##DIESWEEP";

inline constexpr char kClueText[] =
    "Clean with a broom\u0000Clear liquid we drink\u0000Have a meal\u0000Small juicy fruit\u0000Cutting tool with teeth\u0000Go inside\u0000Organ used for hearing\u0000Celebration with guests\u0000Groups with members\u0000Thick slice of meat\u0000Large graceful water bird\u0000Animal ridden with a saddle\u0000Command meaning be quiet\u0000One plus one\u0000Organ used for hearing\u0000Seeds used in chili\u0000Command meaning be quiet\u0000Large antlered animal\u0000Point toward a target\u0000Word that makes a negative\u0000Hard outer covering\u0000Seeds used in chili\u0000Cry of surprise\u0000Glide on an ice blade\u0000Sound of thinking\u0000Command meaning be quiet\u0000Good fortune\u0000Point toward a target\u0000Organ used for hearing\u0000Send a document by phone line\u0000Rest with eyes closed\u0000Cloth worn around the neck\u0000Glide over snow\u0000Blend together\u0000Lights in the night sky\u0000Opposite of down\u0000Tall lifting machine\u0000Thick slice of meat\u0000Clothing worn inside shoes\u0000Wood-chopping tool\u0000Move faster than a walk\u0000Say words aloud\u0000Guide showing places\u0000What we breathe\u0000Happy facial expression\u0000Sugary in taste\u0000Face coverings\u0000Point toward a target\u0000Award for a winner\u0000Grain used to make flour\u0000Sugary in taste\u0000Cry of surprise\u0000Wash in water\u0000Big cat with a mane\u0000Honey-making insect\u0000Cry with short breaths\u0000Very large sea mammal\u0000Neckwear with a knot\u0000A repeated sound\u0000Final part\u0000Small beads of liquid\u0000Go inside\u0000Cutting tool with teeth\u0000Round-ended eating utensil\u0000One-piece garment\u0000Short sleep\u0000One plus one\u0000Baked dish with a crust\u0000Move faster than a walk\u0000Cut tree trunk\u0000Frozen water\u0000Male sheep\u0000Used for washing\u0000Tool filled with ink\u0000What we breathe\u0000Container or situation\u0000Sour yellow fruit\u0000Chart with plotted data\u0000Small digging tool\u0000Short name for microphone\u0000Go inside\u0000Command meaning be quiet\u0000Sense used by the nose\u0000Small sharp fastener\u0000Perform on stage\u0000Our home planet\u0000Clothing worn inside shoes\u0000Point toward a target\u0000Use carelessly\u0000Spool for fishing line\u0000Cutting tool with teeth\u0000Limb from shoulder to hand\u0000Container or situation\u0000Toy flown on a string\u0000Sense used by the nose\u0000Face coverings\u0000Flat dish for food\u0000Single numbered cube\u0000Clean with a broom\u0000Guide showing places\u0000Small digging tool\u0000Neckwear with a knot\u0000Rest with eyes closed\u0000";

inline constexpr Puzzle kPuzzles[] = {
    {0, 0, 5, 5, 8},
    {25, 8, 5, 5, 8},
    {50, 16, 5, 5, 9},
    {75, 25, 5, 5, 9},
    {100, 34, 5, 5, 8},
    {125, 42, 5, 5, 8},
    {150, 50, 5, 5, 10},
    {175, 60, 5, 5, 9},
    {200, 69, 5, 5, 9},
    {225, 78, 5, 5, 8},
    {250, 86, 5, 5, 9},
    {275, 95, 5, 5, 8},
};

inline constexpr Clue kClues[] = {
    {0, 0, 5, 0},
    {19, 10, 5, 0},
    {41, 17, 3, 0},
    {53, 20, 5, 0},
    {71, 0, 3, 1},
    {95, 2, 5, 1},
    {105, 13, 3, 1},
    {128, 4, 5, 1},
    {152, 0, 5, 0},
    {172, 10, 5, 0},
    {192, 15, 4, 0},
    {218, 20, 5, 0},
    {246, 10, 3, 1},
    {271, 11, 3, 1},
    {284, 12, 3, 1},
    {307, 3, 5, 1},
    {327, 2, 3, 0},
    {352, 5, 3, 0},
    {374, 12, 3, 0},
    {396, 15, 3, 0},
    {423, 20, 5, 0},
    {443, 0, 5, 1},
    {463, 16, 2, 1},
    {479, 2, 5, 1},
    {501, 4, 3, 1},
    {519, 2, 3, 0},
    {544, 5, 4, 0},
    {557, 12, 3, 0},
    {579, 15, 3, 0},
    {602, 22, 3, 0},
    {632, 0, 5, 1},
    {654, 2, 5, 1},
    {681, 3, 3, 1},
    {697, 14, 3, 1},
    {712, 0, 5, 0},
    {736, 8, 2, 0},
    {753, 10, 5, 0},
    {774, 20, 5, 0},
    {794, 0, 5, 1},
    {821, 12, 3, 1},
    {840, 3, 3, 1},
    {864, 4, 5, 1},
    {880, 0, 3, 0},
    {901, 5, 3, 0},
    {917, 10, 5, 0},
    {941, 20, 5, 0},
    {957, 0, 5, 1},
    {972, 1, 3, 1},
    {994, 2, 5, 1},
    {1013, 4, 5, 1},
    {1038, 0, 5, 0},
    {1054, 5, 2, 0},
    {1070, 10, 5, 0},
    {1084, 16, 4, 0},
    {1104, 20, 3, 0},
    {1124, 0, 3, 1},
    {1147, 1, 5, 1},
    {1169, 12, 3, 1},
    {1190, 3, 4, 1},
    {1207, 14, 3, 1},
    {1218, 0, 5, 0},
    {1240, 10, 5, 0},
    {1250, 15, 3, 0},
    {1274, 20, 5, 0},
    {1301, 0, 5, 1},
    {1319, 11, 3, 1},
    {1331, 12, 3, 1},
    {1344, 3, 3, 1},
    {1368, 14, 3, 1},
    {1392, 2, 3, 0},
    {1407, 5, 3, 0},
    {1420, 10, 3, 0},
    {1431, 16, 4, 0},
    {1448, 20, 3, 0},
    {1469, 0, 3, 1},
    {1485, 6, 4, 1},
    {1508, 2, 5, 1},
    {1526, 4, 5, 1},
    {1550, 0, 5, 0},
    {1569, 5, 3, 0},
    {1595, 10, 5, 0},
    {1605, 22, 3, 0},
    {1630, 0, 5, 1},
    {1653, 1, 3, 1},
    {1674, 2, 3, 1},
    {1691, 4, 5, 1},
    {1707, 0, 5, 0},
    {1734, 7, 3, 0},
    {1756, 10, 5, 0},
    {1771, 16, 4, 0},
    {1794, 0, 3, 1},
    {1818, 11, 3, 1},
    {1845, 2, 4, 1},
    {1868, 3, 4, 1},
    {1890, 4, 5, 1},
    {1913, 0, 5, 0},
    {1928, 10, 5, 0},
    {1947, 17, 3, 0},
    {1968, 20, 5, 0},
    {1987, 0, 3, 1},
    {2008, 2, 5, 1},
    {2027, 13, 3, 1},
    {2048, 4, 5, 1},
};

inline constexpr uint8_t kPuzzleCount =
    sizeof(kPuzzles) / sizeof(kPuzzles[0]);
static_assert(kPuzzleCount == 12, "crossword puzzle count changed");

}  // namespace crossword_puzzles
