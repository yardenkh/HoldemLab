// Tracks chip payments for the round and hand, then builds and pays
// contribution layers.

#ifndef POKER_POT_MANAGER_HPP
#define POKER_POT_MANAGER_HPP

#include "Pot.hpp"
#include "config/PokerConfig.hpp"
#include "evaluation/HandEvaluator.hpp"
#include "table/Table.hpp"
#include <array>

namespace poker {

/**
 * @brief Manages player contributions, pots, side pots, and pot distribution.
 *
 * The PotManager tracks how much each player has contributed during the
 * current hand and betting round. It can build the required pots and
 * distribute them to the appropriate winners.
 */
class PotManager {
public:
  /**
   * @brief Creates an empty pot manager.
   */
  PotManager();

  /**
   * @brief Resets all contribution and pot state for a new hand.
   */
  void reset_for_new_hand();

  /**
   * @brief Clears only the contributions made during the current betting round.
   *
   * Total contributions for the hand remain unchanged.
   */
  void reset_round_contributions();

  /**
   * @brief Moves chips from a player into their contribution for the pot.
   *
   * If the player does not have enough chips to contribute the requested
   * amount, all of their remaining chips are contributed instead.
   *
   * @param table The table containing the player.
   * @param player_id ID of the player contributing chips.
   * @param requested_amount Number of chips the player is requested to
   * contribute.
   * @return The actual number of chips contributed.
   */
  int contribute(Table &table, int player_id, int requested_amount);

  /**
   * @brief Returns the total amount of chips currently in all pots.
   *
   * @return Total number of contributed chips.
   */
  int total_pot_amount() const;

  /**
   * @brief Returns a player's total contribution during the current hand.
   *
   * @param player_id ID of the player.
   * @return Total number of chips contributed by the player.
   */
  int total_contribution(int player_id) const;

  /**
   * @brief Returns a player's contribution during the current betting round.
   *
   * @param player_id ID of the player.
   * @return Number of chips contributed during the current round.
   */
  int round_contribution(int player_id) const;

  /**
   * @brief Builds the main pot and any required side pots.
   *
   * Pots are created according to each player's total contribution and
   * eligibility to win.
   *
   * @param table The current poker table.
   */
  void build_pots(const Table &table);

  /**
   * @brief Distributes all pots among the eligible winning players.
   *
   * Each pot is awarded independently according to the supplied hand scores.
   * Tied players split the pot.
   *
   * @param table The current poker table.
   * @param hand_scores Evaluated hand score for each player.
   * @return Amount of chips awarded to each player.
   */
  std::array<int, MAX_PLAYERS>
  distribute_pots(Table &table,
                  const std::array<HandScore, MAX_PLAYERS> &hand_scores);

  /**
   * @brief Awards the entire accumulated pot to one player.
   *
   * Used when every other player has folded and no showdown is required.
   * The caller supplies the only remaining player's ID. Pots do not need
   * to be built first; the total comes from the recorded contributions.
   *
   * @param table The current poker table.
   * @param winner_id ID of the winning player.
   * @return Number of chips awarded to the winner, for displaying the result.
   * @throws std::out_of_range if winner_id is not an active seat.
   */
  int award_entire_pot(Table &table, int winner_id);

  /**
   * @brief Returns the number of pots currently built.
   *
   * @return Number of active pots.
   */
  int num_pots() const;

  /**
   * @brief Returns a specific pot.
   *
   * @param pot_index Index of the requested pot.
   * @return Read-only reference to the pot.
   */
  const Pot &pot(int pot_index) const;

private:
  // Total number of chips contributed by each player during the current hand.
  std::array<int, MAX_PLAYERS> total_contributions_{};

  // Number of chips contributed by each player during the current betting
  // round.
  std::array<int, MAX_PLAYERS> round_contributions_{};

  // Main pot and any side pots created during the hand.
  std::array<Pot, MAX_PLAYERS> pots_{};

  // Number of valid pots currently stored in pots_.
  int num_pots_ = 0;
};

} // namespace poker

#endif // POKER_POT_MANAGER_HPP
