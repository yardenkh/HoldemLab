// Shared array capacities and per-match chip/blind configuration.

#ifndef POKER_CONFIG_HPP
#define POKER_CONFIG_HPP

namespace poker {

// Fixed capacities size the engine's arrays; they are not current dealt counts.
inline constexpr int MAX_PLAYERS = 8;
inline constexpr int DECK_SIZE = 52;
inline constexpr int HOLE_CARDS_PER_PLAYER = 2;
inline constexpr int MAX_COMMUNITY_CARDS = 5;

// Defaults for a newly configured match. Chips are whole integers; no rake is
// deducted, and blinds remain at these configured values throughout the match.
inline constexpr int DEFAULT_STARTING_CHIPS = 2500;
inline constexpr int DEFAULT_SMALL_BLIND = 50;
inline constexpr int DEFAULT_BIG_BLIND = 100;

// Per-game starting stacks and forced bets. PokerGame validates these values
// before play; an episode reset can supply different stacks for research.
struct GameConfig {
  int starting_chips = DEFAULT_STARTING_CHIPS;
  int small_blind = DEFAULT_SMALL_BLIND;
  int big_blind = DEFAULT_BIG_BLIND;
};

} // namespace poker

#endif
