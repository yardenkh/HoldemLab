// Coordinates complete PokerGame hands without duplicating betting/payout
// rules.
#pragma once
#include "game/PokerGame.hpp"
#include "tournament/TournamentConfig.hpp"
#include "tournament/TournamentObserver.hpp"
#include "tournament/TournamentResult.hpp"

namespace poker {
/**
 * @brief A single-table freezeout tournament around the existing hand engine.
 * Players retain their chips between hands; zero chips after payout means
 * elimination. Simultaneous eliminations rank by stacks before blind payments.
 * Equal starting stacks tie across the occupied finishing positions and share
 * those prizes. Any indivisible prize units go to tied players in seat-ID
 * order.
 *
 * Providers and the optional GameObserver are borrowed and must outlive use.
 * Providers choose actions; observers see ordinary hand events. Tournament
 * standings are updated after a complete hand returns, so read result() after
 * play_one_hand(), not inside the engine's on_hand_finished callback.
 * Observers should not throw. A provider error propagates; after fixing the
 * provider, play_one_hand() can resume the same hand and original starting
 * stacks.
 *
 * Hand observers and tournament observers are separate optional listeners.
 * There is no re-entry, multiple-table balancing, or ICM implementation.
 */
class Tournament {
public:
  Tournament(
      int num_players, const TournamentConfig &config,
      std::uint32_t seed = 5489u,
      const FastHandEvaluator &evaluator = default_fast_hand_evaluator());
  void set_decision_provider(int player_id, DecisionProvider &provider);
  void set_observer(GameObserver *observer);
  void set_tournament_observer(TournamentObserver *observer);
  // Reuse the same game/configuration and borrowed dependencies for another
  // tournament. Reset abandons a pending hand and clears all finishes/prizes.
  void reset(std::uint32_t seed);
  std::uint64_t decisions_applied() const { return game_.decisions_applied(); }

  // Play one complete hand, or resume it after a provider error. Returns false
  // if already finished; completed tournaments never award prizes a second
  // time.
  bool play_one_hand();
  // max_hands <= 0 runs to a winner. A positive limit pauses after that many
  // additional hands; standings can be inspected and run() called again later.
  void run(int max_hands = 0);
  bool finished() const { return result_.winner_id >= 0; }
  int winner_id() const { return result_.winner_id; }
  const TournamentResult &result() const { return result_; }
  const Table &table() const { return game_.table(); }
  // The level for the most recently begun hand, or level zero before play.
  std::size_t blind_level_index() const { return level_; }
  const BlindLevel &blind_level() const { return config_.blind_levels[level_]; }

private:
  TournamentConfig config_;
  PokerGame game_;
  TournamentResult result_;
  std::size_t level_ = 0;
  std::uint64_t hands_at_level_ = 0;
  bool hand_pending_ = false;
  bool started_ = false;
  TournamentObserver *tournament_observer_ = nullptr;
  // Captured before blinds, and preserved if a provider error interrupts play.
  std::array<int, MAX_PLAYERS> starting_stacks_{};
  static GameConfig validate_config(int num_players,
                                    const TournamentConfig &config);
  std::int64_t prize_for_place(int place) const;
  void record_finished_hand();
};
} // namespace poker
