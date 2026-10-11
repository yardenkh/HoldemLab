// Moves chips, constructs side pots, finds tied winners, and pays whole-chip
// shares.

#include "PotManager.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace poker {

// Observers can inspect built pots before payouts clear the pot records.
int PotManager::num_pots() const { return num_pots_; }
const Pot &PotManager::pot(int pot_index) const {
  if (pot_index < 0 || pot_index >= num_pots_) {
    throw std::out_of_range("Pot index is not a built pot");
  }
  return pots_[pot_index];
}

// The member initializers in the header start every contribution and pot at
// zero.
PotManager::PotManager() = default;

void PotManager::reset_for_new_hand() {
  // Clear both hand totals and round totals, plus any previously built pots.
  total_contributions_.fill(0);
  round_contributions_.fill(0);
  pots_.fill(Pot{});
  num_pots_ = 0;
}

// A new street starts fresh betting totals without losing money paid earlier.
void PotManager::reset_round_contributions() { round_contributions_.fill(0); }

int PotManager::contribute(Table &table, int player_id, int requested_amount) {
  if (requested_amount <= 0) {
    return 0;
  }

  Player &p = table.players[player_id];
  int amount_to_contribute = std::min(p.cash, requested_amount);
  // Paying every remaining chip marks the player all-in. For example, a
  // requested 100 with only 60 cash contributes 60 and leaves no cash to act.
  p.all_in = (p.cash == amount_to_contribute);

  p.cash -= amount_to_contribute;

  total_contributions_[player_id] += amount_to_contribute;
  // Record the same payment in two time frames: this hand and this street.
  round_contributions_[player_id] += amount_to_contribute;
  // Report the amount actually paid, which can be less than requested.
  return amount_to_contribute;
}

int PotManager::total_pot_amount() const {
  // Sum contribution records, so this works before build_pots is called too.
  return std::accumulate(total_contributions_.cbegin(),
                         total_contributions_.cend(), 0);
}

int PotManager::total_contribution(int player_id) const {
  return total_contributions_[player_id];
}

int PotManager::round_contribution(int player_id) const {
  return round_contributions_[player_id];
}

void PotManager::build_pots(const Table &table) {
  // Clear pots from any previous build.
  pots_.fill(Pot{});
  num_pots_ = 0;

  // We sort a copy because total_contributions_[player_id] must remain
  // associated with the correct player.
  std::array<int, MAX_PLAYERS> sorted_contributions = total_contributions_;

  std::sort(sorted_contributions.begin(), sorted_contributions.end());

  int previous_level = 0;
  // Example totals 50, 100, 100 produce a main pot of 50*3 = 150 and a
  // side pot of (100-50)*2 = 100. Sorting totals finds those layer boundaries.

  for (int current_level : sorted_contributions) {
    // Players who did not contribute do not create a pot level.
    if (current_level == 0) {
      continue;
    }

    // Skip duplicate contribution levels.
    if (current_level == previous_level) {
      continue;
    }

    Pot &current_pot = pots_[num_pots_];

    int num_contributors = 0;

    // Find every player who contributed at least up to this level.
    for (int player_id = 0; player_id < table.num_players; player_id++) {
      if (total_contributions_[player_id] >= current_level) {
        num_contributors++;

        // Folded players' chips remain in the pot, but they cannot win it.
        if (table.players[player_id].in_hand) {
          current_pot.eligible_players[player_id] = true;
        }
      }
    }

    // Only the contribution between the previous level and this level
    // belongs to this pot.
    int contribution_per_player = current_level - previous_level;

    current_pot.amount = contribution_per_player * num_contributors;

    num_pots_++;

    previous_level = current_level;
  }
}

std::array<int, MAX_PLAYERS> PotManager::distribute_pots(
    Table &table, const std::array<HandScore, MAX_PLAYERS> &hand_scores) {

  // winnings[player_id] stores how many chips that player wins in total.
  // For example, winnings[2] is the total won by player 2.
  // The empty braces {} make all these amounts start at zero.
  std::array<int, MAX_PLAYERS> winnings{};

  // num_pots_ tells us how many entries in pots_ are ready to be paid out.
  // The trailing underscore is our naming convention for private class data.
  // Check each pot separately: a player may be allowed to win
  // the main pot but may not have contributed enough to win a particular side
  // pot.
  for (int pot_index = 0; pot_index < num_pots_; ++pot_index) {
    // current_pot is another name for pots_[pot_index].
    // const means we can read this pot here but cannot change it.
    const Pot &current_pot = pots_[pot_index];

    if (current_pot.amount <= 0) {
      continue;
    }

    // Start a fresh search for this pot's winners.
    // best_score is the strongest hand score found so far.
    // winners stores player IDs; winner_count tells us how many are valid.
    // For example, if winner_count is 2, use winners[0] and winners[1].
    HandScore best_score = 0;
    std::array<int, MAX_PLAYERS> winners{};
    int winner_count = 0;

    // Look at every player who is allowed to win this pot.
    for (int player_id = 0; player_id < table.num_players; ++player_id) {
      if (!current_pot.eligible_players[player_id]) {
        // false means this player cannot win this pot, so skip them.
        continue;
      }

      const HandScore score = hand_scores[player_id];

      if (winner_count == 0 || score > best_score) {
        // Keep this player if we have no winner yet or their hand is
        // stronger than the best hand found so far. A bigger score is better.
        best_score = score;
        winner_count = 0;
        // Setting winner_count to zero above discards the earlier winners.
        // Save this player's ID, then increase winner_count by one.
        winners[winner_count++] = player_id;
      } else if (score == best_score) {
        // This player has the same hand strength as the current winners,
        // so add them to the list of players who will share this pot.
        winners[winner_count++] = player_id;
      }
    }

    if (winner_count == 0) {
      // There are chips in this pot, but nobody is allowed to win them.
      // Stop with an error. No player's cash has been changed yet.
      throw std::logic_error("A nonempty pot has no eligible players");
    }

    // share is the number of chips each winner gets.
    // remainder is the number left over because chips cannot be split.
    // For example, 101 chips split between 2 winners gives 50 each,
    // with 1 chip left over. / calculates the share; % calculates what is left.
    const int share = current_pot.amount / winner_count;
    int remainder = current_pot.amount % winner_count;

    for (int i = 0; i < winner_count; ++i) {
      // winners[i] gives us a winning player's ID.
      // Add their share to their total; they may also win other pots.
      winnings[winners[i]] += share;
    }

    // Give each leftover chip to a winner, starting with the first winner
    // after the dealer in seat order. Stop when no leftover chips remain.
    for (int offset = 1; offset <= table.num_players && remainder > 0;
         ++offset) {

      // offset is how many seats we have moved past the dealer.
      // % table.num_players brings us back to seat 0 after the last seat.
      const int player_id = (table.dealer_id + offset) % table.num_players;

      // Give an extra chip only if this player is allowed to win this pot
      // and their hand score matches the winning score.
      if (current_pot.eligible_players[player_id] &&
          hand_scores[player_id] == best_score) {
        ++winnings[player_id];
        --remainder;
      }
    }
  }

  // All pots have now been checked. Add each player's total winnings
  // to their cash. Waiting until here avoids partial payment on an error.
  for (int player_id = 0; player_id < table.num_players; ++player_id) {
    table.players[player_id].cash += winnings[player_id];
  }

  // Empty the pots and clear the recorded contributions now that they
  // have been paid. This prevents paying the same chips again.
  // This function resets only PotManager's data; players keep their cash.
  reset_for_new_hand();

  // Return how much each player won, for example so the UI can display it.
  return winnings;
}

int PotManager::award_entire_pot(Table &table, int winner_id) {
  // winner_id selects the player who will receive the chips.
  // Reject an invalid seat before changing cash or clearing any pot records.
  if (winner_id < 0 || winner_id >= table.num_players ||
      winner_id >= MAX_PLAYERS) {
    throw std::out_of_range("Winner ID is not an active seat");
  }

  // Sum every player's contribution, including chips from folded players.
  // This works whether pots_ has been built yet or contains several pots.
  const int amount_won = total_pot_amount();

  // The caller uses this function when only this player remains in the hand,
  // so no hand-score comparisons or splitting between winners are needed.
  table.players[winner_id].cash += amount_won;

  // Clear all contributions and pots so these chips cannot be awarded twice.
  // This resets PotManager's records; the winner keeps their updated cash.
  reset_for_new_hand();

  // Return just the amount, so the UI can show "Player 2 won 150 chips."
  return amount_won;
}

} // namespace poker
