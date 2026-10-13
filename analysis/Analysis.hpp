// Small analysis tools that read player observations without changing a game.
#pragma once
#include "evaluation/FastHandEvaluator.hpp"
#include "view/PlayerView.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace poker {
/**
 * @brief An equally likely set of two-card hands for one specific opponent.
 * player_id identifies a table seat, not its position in the range list.
 * Each array in hands is one physical combination, for example As Ks.
 * Repeated combinations (including reversed card order) count only once.
 * These are assumptions for analysis; they never change the real dealt cards.
 */
struct OpponentRange {
  int player_id = -1;
  std::vector<std::array<Card, 2>> hands;
};
/**
 * @brief Calculates poker statistics from the information visible to one
 * player.
 *
 * calculate_equity() estimates the player's
 * average share of one hypothetical pot if everyone still in the hand reaches
 * showdown. For example, winning alone contributes 1.0, a three-way winning tie
 * contributes 1.0 / 3.0, and losing contributes 0.0. Averaging these shares
 * over many randomly completed deals gives an equity value between 0.0 and 1.0.
 * Therefore 0.5 means an estimated 50% pot share, including ties; it does not
 * necessarily mean winning outright in 50% of the deals.
 *
 * The calculation reads a PlayerView: the player's own two cards, the visible
 * community cards, and which opponents remain in the hand. Opponents' private
 * cards and the missing community cards are sampled without duplicate
 * physical cards. Folded opponents are excluded; all-in opponents still
 * compete. The requesting player can occupy any seat, not just player 0.
 *
 * Analysis does not change the live game or choose an action for the player.
 * The basic method assumes random opponents. The range overload can restrict
 * selected opponents to equally likely sets of hands supplied by the caller.
 * calculate_icm() separately converts remaining tournament stacks and prizes
 * into expected prize values under the Independent Chip Model. It uses no cards
 * or random samples and does not predict player skill or future blind pressure.
 * Neither tool models future betting, side pots, weighted ranges, or chooses
 * profitable actions by itself.
 * An AI can use the returned equity alongside other information to decide what
 * to do. Other analysis methods can be added here as the project grows.
 *
 * The class borrows a shared FastHandEvaluator, so it does not load a separate
 * lookup table. Each equity calculation uses its own seeded random generator
 * and small reusable arrays, with no full game copies or per-sample heap
 * allocations. If you supply a custom evaluator, keep it alive for this
 * object's lifetime.
 */
class Analysis {
public:
  // Borrow the shared lookup evaluator instead of loading another table.
  // An explicitly supplied evaluator must outlive this Analysis object.
  explicit Analysis(
      const FastHandEvaluator &evaluator = default_fast_hand_evaluator());

  /**
   * @brief Estimate the view's player's showdown equity by random sampling.
   * @param view The player's observation, with two private cards and 0..5 known
   *             board cards. The selected player must still be in the hand.
   * @param sample_count Number of hypothetical deals to evaluate; must be
   * positive. More samples usually reduce sampling error but take longer.
   * @param seed Seed for this call's random generator. Identical inputs and
   * seed reproduce the result on the same runtime. Change it if you want a new
   * random estimate instead of repeating the previous one.
   * @return Average pot share from 0.0 to 1.0, or 1.0 if every opponent folded.
   * @throws std::invalid_argument If counts, player ID, known cards, or the
   *                              requesting player's participation are invalid.
   */
  [[nodiscard]] double calculate_equity(const PlayerView &view,
                                        int sample_count,
                                        std::uint32_t seed = 5489u) const;

  /**
   * @brief Estimate equity against explicit hand sets for selected opponents.
   * @param opponent_ranges Ranges indexed by their player_id. Opponents without
   *                        a supplied range still receive uniformly random
   * cards. Empty input delegates to the original fast method. Known-card
   * conflicts are removed and reversed/duplicate combinations are merged before
   * sampling. Supplied hands are chosen independently, then the entire ranged
   * assignment is rejected if cards overlap. This preserves equal probability
   * over legal joint assignments instead of favouring whichever opponent is
   * sampled first. Preparation may allocate, but the sampling loop reuses
   * fixed-size buffers.
   * @throws std::invalid_argument For invalid requests/ranges or when no legal
   *                              joint assignment exists.
   * @throws std::runtime_error If compatibility checking exceeds 1,000,000 hand
   *                           visits, or a sample needs over 10,000 proposals.
   *                           No partial estimate is returned on failure.
   */
  [[nodiscard]] double
  calculate_equity(const PlayerView &view, int sample_count, std::uint32_t seed,
                   std::span<const OpponentRange> opponent_ranges) const;

  /**
   * @brief Calculate expected tournament prizes under the Independent Chip
   * Model.
   *
   * ICM assigns the next finishing position proportionally to the chip stacks
   * of players not yet placed. After assigning first place, it removes that
   * player and applies the same rule to second place, and so on. A player's
   * expected prize is the sum of each place's prize times their probability of
   * finishing there. The computation is exact under this model, up to ordinary
   * floating-point rounding; it is not an exact prediction of real tournament
   * play.
   *
   * @param stacks Positive chip stacks of 1..MAX_PLAYERS surviving players.
   *               Use settled stacks, not temporarily empty all-in balances.
   * @param prizes Remaining prizes in descending finishing-place order, in
   *               integer prize units such as cents. Entries must be
   * nonnegative. There may be fewer prizes than players; unpaid places are
   * zero. An empty prize list means everyone receives zero.
   * @return Expected prize units for each supplied player: result[0] belongs
   *         to stacks[0], etc. Entries beyond stacks.size() remain zero. This
   *         is money value, not showdown equity or net profit after an entry
   * fee.
   *
   * Supply only survivors and prizes still available to them. Already awarded
   * elimination prizes are accounted for separately. If stacks are compacted
   * from table seats, the caller must retain that mapping to original player
   * IDs. This static method needs no Analysis object or loaded hand-evaluation
   * table. It uses fixed local arrays with at most 256 subset states and no
   * allocation.
   * @throws std::invalid_argument For invalid counts/stacks, increasing or
   * negative prizes, or an integer-overflowing total prize pool.
   */
  [[nodiscard]] static std::array<double, MAX_PLAYERS>
  calculate_icm(std::span<const int> stacks,
                std::span<const std::int64_t> prizes);

private:
  // Sampling state is local to each call; only immutable evaluator data is
  // shared.
  const FastHandEvaluator &evaluator_;
  // Keep input checks separate from the mathematical recurrence. Validation
  // runs once before creating subset states or dividing by remaining chips.
  static void validate_icm_inputs(std::span<const int> stacks,
                                  std::span<const std::int64_t> prizes);
};
} // namespace poker
