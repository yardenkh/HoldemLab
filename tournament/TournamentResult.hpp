// Standings retain original seat IDs even as players are eliminated.
#pragma once
#include "config/PokerConfig.hpp"
#include <array>
#include <cstdint>

namespace poker {
/** One player's standing. Unfinished players have both positions set to zero.
 * A normal finish has equal first_place/last_place, such as 3/3 for third.
 * A tie covering second and third is recorded as 2/3 for each tied player.
 * prize is money in the configured integer units, never the final chip stack.
 */
struct TournamentPlayerResult {
  int first_place = 0;
  int last_place = 0;
  std::int64_t prize = 0;
  std::uint64_t eliminated_on_hand =
      0; // Zero for the winner or an active player.
};

struct TournamentResult {
  int num_players = 0;
  int players_remaining = 0;
  std::uint64_t hands_played = 0;
  int winner_id = -1; // Set only when the tournament has finished.
  // players[2] is always the result for the original player at seat 2.
  std::array<TournamentPlayerResult, MAX_PLAYERS> players{};
};
} // namespace poker
