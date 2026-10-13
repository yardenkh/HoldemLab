// Tournament settings are separate from the rules used to play a single hand.
#pragma once
#include "config/PokerConfig.hpp"
#include <cstdint>
#include <vector>

namespace poker {
/** One blind level. It applies for hands_per_level completed hands; the final
 * level continues indefinitely until a winner remains. Levels change between
 * hands, never while a player is deciding or a pot is being settled. */
struct BlindLevel {
  int small_blind = DEFAULT_SMALL_BLIND;
  int big_blind = DEFAULT_BIG_BLIND;
  int hands_per_level = 10;
};

/** Settings for a single-table freezeout: 2..8 players, equal starting stacks,
 * no re-entry, and blind progression measured in hands rather than wall time.
 * Prize units are caller-defined integers (for example cents), not poker chips.
 * Supply prizes explicitly; first entry is first place, second is second place,
 * etc. Omitted lower positions pay zero. Prizes must be nonnegative and
 * descend.
 */
struct TournamentConfig {
  int starting_chips = DEFAULT_STARTING_CHIPS;
  std::vector<BlindLevel> blind_levels{{50, 100, 10},
                                       {100, 200, 10},
                                       {200, 400, 10},
                                       {400, 800, 10},
                                       {800, 1600, 10}};
  std::vector<std::int64_t> prizes;
};
} // namespace poker
