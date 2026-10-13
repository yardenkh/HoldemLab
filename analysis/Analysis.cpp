// Estimate showdown equity using reusable arrays, without copying a live game.
#include "analysis/Analysis.hpp"
#include "simulation/CardSampler.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <utility>

namespace poker {
// Store a reference to the evaluator supplied by the caller. The default
// evaluator is shared with the engine; constructing Analysis does not duplicate
// its lookup data. The caller must keep a custom evaluator alive while using
// us.
Analysis::Analysis(const FastHandEvaluator &evaluator)
    : evaluator_(evaluator) {}

double Analysis::calculate_equity(const PlayerView &view, int sample_count,
                                  std::uint32_t seed) const {
  // Reject invalid requests before indexing any player/card arrays. Hold'em
  // requires exactly two private cards, and the visible board can contain at
  // most five cards. A positive sample count also prevents division by zero.
  // The fast evaluator assumes valid cards, so validation belongs outside the
  // repeated sampling loop rather than inside every hand evaluation.
  if (sample_count <= 0 || view.num_players < 2 ||
      view.num_players > MAX_PLAYERS || view.player_id < 0 ||
      view.player_id >= view.num_players || view.hole_card_count != 2 ||
      view.community_card_count < 0 || view.community_card_count > 5)
    throw std::invalid_argument("Invalid equity input or sample count");
  // This player must be competing for the hypothetical pot. Folded players
  // cannot win it, even if their private cards would have made the best hand.
  if (!view.players_in_hand[view.player_id])
    throw std::invalid_argument("Cannot calculate equity for a folded player");

  // Build a list of cards unavailable for sampling: our two private cards,
  // followed by only the community cards already visible. For example, on the
  // flop this list contains five cards in total: our two plus the three board
  // cards. Entries beyond that count are unused, even if the array holds data.
  std::array<Card, 7> known{};
  known[0] = view.hole_cards[0];
  known[1] = view.hole_cards[1];
  std::copy_n(view.community_cards.begin(), view.community_card_count,
              known.begin() + 2);
  // The span is a non-owning view of just the used portion of known[]. It does
  // not copy the array. CardSampler builds the remaining-card pool once and
  // rejects invalid ranks/suits or duplicate known cards before sampling
  // starts.
  CardSampler sampler(
      std::span<const Card>(known.data(), 2 + view.community_card_count));
  // We need only the number of opponents, because this first version assigns
  // all of them the same uniformly random hand distribution. Include all-in
  // players: they cannot bet again, but their cards can still win at showdown.
  int opponents = 0;
  for (int id = 0; id < view.num_players; ++id)
    if (id != view.player_id && view.players_in_hand[id])
      ++opponents;
  // No simulation is necessary when nobody remains to challenge our player.
  // Known-card validation above still runs, even for this early return.
  if (opponents == 0)
    return 1.0; // Everyone else folded.

  // Each draw supplies the missing board cards plus two cards per opponent.
  // On a flop against two opponents, that is 2 + 2*2 = 6 sampled cards.
  const int missing_board = 5 - view.community_card_count;
  const int cards_to_sample = missing_board + 2 * opponents;
  // Copy the five-slot board once, keeping its visible prefix fixed. The sample
  // buffer has room for the largest request: five board cards and seven pairs
  // of opponent cards. These fixed local arrays are reused for every sample;
  // no full Table/PokerGame copy or per-sample heap allocation is needed.
  std::array<Card, 5> board = view.community_cards;
  std::array<Card, 5 + 2 * (MAX_PLAYERS - 1)> sampled{};
  // Use our own seeded generator, so analysis cannot advance the live game's
  // random generator or change which cards the real game will deal next.
  std::mt19937 rng(seed);
  // This is a sum of pot fractions, not a count of outright wins or chip
  // profit.
  double total_share = 0.0;
  for (int sample = 0; sample < sample_count; ++sample) {
    // Draw every unknown card together without replacement. This prevents a
    // physical card from appearing in two hands or both a hand and the board.
    // CardSampler retains the full available pool between calls; it does not
    // permanently consume cards, so a later sample may draw those cards again.
    sampler.sample(std::span<Card>(sampled.data(), cards_to_sample), rng);
    // The first missing_board entries belong to the board. Leave the visible
    // cards unchanged and overwrite every undealt board slot before scoring.
    std::copy_n(sampled.begin(), missing_board,
                board.begin() + view.community_card_count);
    // Score our two fixed cards with this sample's completed five-card board.
    // Dense lookup scores can be compared directly: larger means stronger.
    const auto our_score = evaluator_.evaluate(
        known[0], known[1], board[0], board[1], board[2], board[3], board[4]);
    // Start by assuming our player is among the winners. Every equal opponent
    // increases the number sharing the pot; a stronger opponent cancels that
    // assumption. A weaker opponent makes no difference to our share.
    int tied_winners =
        1; // Count ourselves and any opponents matching our score.
    bool we_win = true;
    for (int opponent = 0; opponent < opponents; ++opponent) {
      // Opponent pairs follow the board cards in sampled[]. With two missing
      // board cards, opponent 0 uses positions 2 and 3; opponent 1 uses 4
      // and 5.
      const int offset = missing_board + 2 * opponent;
      const auto score =
          evaluator_.evaluate(sampled[offset], sampled[offset + 1], board[0],
                              board[1], board[2], board[3], board[4]);
      if (score > our_score) {
        // Once beaten, our share is zero regardless of the remaining hands.
        // Any ties counted earlier are now irrelevant, so stop scoring
        // opponents for this sample. This early exit saves work without
        // changing the result.
        we_win = false;
        break;
      }
      if (score == our_score)
        ++tied_winners;
    }
    // A sole win contributes 1.0; a two-way winning tie contributes 0.5; a
    // three-way winning tie contributes one third. Losing contributes nothing.
    // 1.0 makes this floating-point division rather than integer division.
    if (we_win)
      total_share += 1.0 / tied_winners;
  }
  // Average the shares over all samples, including the ones where we lost.
  // For example, total_share=600 over 1,000 samples means estimated equity 0.6.
  return total_share / sample_count;
}
// Helpers in this unnamed namespace belong only to this source file. They sit
// inside poker, while the public Analysis methods share the same outer block.
namespace {
// A prepared combination carries a 52-bit physical-card mask. Checking two
// hands for overlap then needs only a bitwise AND rather than nested searches.
struct RangeHand {
  std::array<Card, 2> cards;
  std::uint64_t mask;
};
struct PreparedRange {
  int player_id;
  std::vector<RangeHand> hands;
};
std::uint64_t card_mask(Card card) {
  const int rank = static_cast<int>(card.face) - 2;
  const int suit = static_cast<int>(card.suit);
  if (rank < 0 || rank >= 13 || suit < 0 || suit >= 4)
    throw std::invalid_argument("Range contains an invalid card");
  return std::uint64_t{1} << (suit * 13 + rank);
}

// Search only for one legal assignment, not every combination. Small ranges
// go first to expose conflicts early. Bound the work so difficult inputs cannot
// hang analysis; exceeding the limit is distinct from proving impossibility.
bool compatible_assignment(std::span<const PreparedRange> ranges,
                           std::size_t next, std::uint64_t used,
                           std::size_t &visits) {
  if (next == ranges.size())
    return true;
  for (const auto &hand : ranges[next].hands) {
    if (++visits > 1000000)
      throw std::runtime_error(
          "Equity range compatibility search limit exceeded");
    if (!(used & hand.mask) &&
        compatible_assignment(ranges, next + 1, used | hand.mask, visits))
      return true;
  }
  return false;
}
} // namespace

double Analysis::calculate_equity(
    const PlayerView &view, int sample_count, std::uint32_t seed,
    std::span<const OpponentRange> opponent_ranges) const {
  // Preserve the exact original code path and seeded result for empty input.
  if (opponent_ranges.empty())
    return calculate_equity(view, sample_count, seed);
  if (sample_count <= 0 || view.num_players < 2 ||
      view.num_players > MAX_PLAYERS || view.player_id < 0 ||
      view.player_id >= view.num_players || view.hole_card_count != 2 ||
      view.community_card_count < 0 || view.community_card_count > 5 ||
      !view.players_in_hand[view.player_id])
    throw std::invalid_argument(
        "Invalid range equity observation or sample count");

  // This buffer holds the visible cards plus the sampled ranged hands. The
  // sampler validates the visible prefix before any unchecked hand evaluation.
  std::array<Card, 7 + 2 * (MAX_PLAYERS - 1)> excluded{};
  excluded[0] = view.hole_cards[0];
  excluded[1] = view.hole_cards[1];
  std::copy_n(view.community_cards.begin(), view.community_card_count,
              excluded.begin() + 2);
  const int known_count = 2 + view.community_card_count;
  CardSampler sampler(std::span<const Card>(excluded.data(), known_count));
  std::uint64_t known_mask = 0;
  for (int i = 0; i < known_count; ++i)
    known_mask |= card_mask(excluded[i]);

  std::array<bool, MAX_PLAYERS> has_range{};
  std::vector<PreparedRange> prepared;
  prepared.reserve(
      std::min<std::size_t>(opponent_ranges.size(), MAX_PLAYERS - 1));
  for (const auto &range : opponent_ranges) {
    const int id = range.player_id;
    if (id < 0 || id >= view.num_players || id == view.player_id ||
        !view.players_in_hand[id] || has_range[id] || range.hands.empty())
      throw std::invalid_argument(
          "Range must identify a unique active opponent and contain hands");
    has_range[id] = true;
    PreparedRange filtered{id, {}};
    filtered.hands.reserve(range.hands.size());
    for (auto cards : range.hands) {
      const auto first = card_mask(cards[0]), second = card_mask(cards[1]);
      if (first == second)
        throw std::invalid_argument("Range hand repeats a physical card");
      // Canonical card order plus deduplication makes As Ks and Ks As one
      // equally likely hand. A repeated entry must not silently add weight.
      if (first > second)
        std::swap(cards[0], cards[1]);
      const auto mask = first | second;
      if (!(mask & known_mask))
        filtered.hands.push_back({cards, mask});
    }
    std::sort(
        filtered.hands.begin(), filtered.hands.end(),
        [](const RangeHand &a, const RangeHand &b) { return a.mask < b.mask; });
    filtered.hands.erase(
        std::unique(filtered.hands.begin(), filtered.hands.end(),
                    [](const RangeHand &a, const RangeHand &b) {
                      return a.mask == b.mask;
                    }),
        filtered.hands.end());
    if (filtered.hands.empty())
      throw std::invalid_argument(
          "Range has no hands compatible with visible cards");
    prepared.push_back(std::move(filtered));
  }
  std::sort(prepared.begin(), prepared.end(),
            [](const PreparedRange &a, const PreparedRange &b) {
              return a.hands.size() != b.hands.size()
                         ? a.hands.size() < b.hands.size()
                         : a.player_id < b.player_id;
            });
  std::size_t visits = 0;
  if (!compatible_assignment(prepared, 0, known_mask, visits))
    throw std::invalid_argument(
        "Opponent ranges have no compatible joint assignment");

  int random_opponents = 0;
  for (int id = 0; id < view.num_players; ++id)
    if (id != view.player_id && view.players_in_hand[id] && !has_range[id])
      ++random_opponents;
  const int missing_board = 5 - view.community_card_count;
  const int random_count = missing_board + 2 * random_opponents;
  std::array<Card, 5> board = view.community_cards;
  std::array<std::array<Card, 2>, MAX_PLAYERS> hands{};
  std::array<Card, 5 + 2 * (MAX_PLAYERS - 1)> drawn{};
  std::mt19937 rng(seed);
  double total_share = 0;
  for (int sample = 0; sample < sample_count; ++sample) {
    bool accepted = false;
    int excluded_count = known_count;
    for (int attempt = 0; attempt < 10000; ++attempt) {
      auto used = known_mask;
      excluded_count = known_count;
      bool collision = false;
      // Propose each ranged hand independently from its full prepared set.
      // On any collision restart the ENTIRE proposal, including earlier hands.
      // Keeping earlier choices and merely restricting the next range is
      // biased.
      for (const auto &range : prepared) {
        const auto &hand =
            range.hands[std::uniform_int_distribution<std::size_t>(
                0, range.hands.size() - 1)(rng)];
        if (used & hand.mask) {
          collision = true;
          break;
        }
        used |= hand.mask;
        hands[range.player_id] = hand.cards;
        excluded[excluded_count++] = hand.cards[0];
        excluded[excluded_count++] = hand.cards[1];
      }
      if (!collision) {
        accepted = true;
        break;
      }
    }
    if (!accepted)
      throw std::runtime_error(
          "Equity range sampling exceeded 10,000 proposals for a sample");

    // Every accepted ranged assignment uses the same number of cards, hence
    // has the same number of random-hand/board completions. We can draw these
    // directly from its remaining pool without bias or further rejection.
    sampler.reset(std::span<const Card>(excluded.data(), excluded_count));
    sampler.sample(std::span<Card>(drawn.data(), random_count), rng);
    std::copy_n(drawn.begin(), missing_board,
                board.begin() + view.community_card_count);
    int offset = missing_board;
    for (int id = 0; id < view.num_players; ++id)
      if (id != view.player_id && view.players_in_hand[id] && !has_range[id]) {
        hands[id] = {drawn[offset], drawn[offset + 1]};
        offset += 2;
      }
    const auto ours =
        evaluator_.evaluate(view.hole_cards[0], view.hole_cards[1], board[0],
                            board[1], board[2], board[3], board[4]);
    bool we_win = true;
    int tied = 1;
    for (int id = 0; id < view.num_players; ++id) {
      if (id == view.player_id || !view.players_in_hand[id])
        continue;
      const auto score =
          evaluator_.evaluate(hands[id][0], hands[id][1], board[0], board[1],
                              board[2], board[3], board[4]);
      if (score > ours) {
        we_win = false;
        break;
      }
      if (score == ours)
        ++tied;
    }
    // A failed proposal is not a poker loss. Only accepted, completed deals
    // enter the average; errors never return an unfinished estimate.
    if (we_win)
      total_share += 1.0 / tied;
  }
  return total_share / sample_count;
}
// Validate the standalone ICM inputs, independently of a particular Tournament.
// Prize units match the tournament API; input positions identify the players.
void Analysis::validate_icm_inputs(std::span<const int> stacks,
                                   std::span<const std::int64_t> prizes) {
  // Each supplied stack represents a surviving player. Zero during an all-in
  // hand is not elimination: the caller must settle that hypothetical/real hand
  // first, award any elimination prizes separately, and supply only survivors.
  if (stacks.empty() || stacks.size() > MAX_PLAYERS ||
      prizes.size() > stacks.size())
    throw std::invalid_argument(
        "ICM needs 1..8 surviving stacks and at most one prize per place");
  for (int stack : stacks)
    if (stack <= 0)
      throw std::invalid_argument("ICM survivor stacks must be positive");

  std::int64_t pool = 0;
  for (std::size_t place = 0; place < prizes.size(); ++place) {
    const auto prize = prizes[place];
    // Equal prizes are allowed. Match the tournament's integer prize contract
    // and reject an overflowing pool rather than silently wrapping money units.
    if (prize < 0 || (place > 0 && prize > prizes[place - 1]) ||
        prize > std::numeric_limits<std::int64_t>::max() - pool)
      throw std::invalid_argument("Invalid ICM prize order, amount or total");
    pool += prize;
  }
}

// Exact subset-based ICM: stack proportions assign places, and prize-weighted
// probabilities give each player's expected winnings. This uses no
// evaluator/RNG.
std::array<double, MAX_PLAYERS>
Analysis::calculate_icm(std::span<const int> stacks,
                        std::span<const std::int64_t> prizes) {
  validate_icm_inputs(stacks, prizes);
  std::array<double, MAX_PLAYERS> expected{};
  if (prizes.empty())
    return expected; // No available money means zero value.

  // A mask records which players have already received the higher positions.
  // Bit 0 represents stacks[0], bit 1 stacks[1], etc. With three players,
  // mask 101 (binary) means players 0 and 2 have already been placed. It does
  // not specify which of them finished first: all paths to that group are
  // merged.
  constexpr unsigned max_states = 1u << MAX_PLAYERS;
  const unsigned states = 1u << stacks.size();
  std::array<double, max_states> probability{};
  probability[0] =
      1.0; // Initially nobody is placed; reaching this state is certain.

  // Adding an unplaced player's bit always increases the numeric mask, so this
  // loop visits a group only after every smaller predecessor has contributed.
  // This combines equivalent prefixes instead of enumerating n! finish orders.
  for (unsigned mask = 0; mask < states; ++mask) {
    const auto place = static_cast<std::size_t>(std::popcount(mask));
    // 'place' is zero-based: zero assigned players means first prize is next.
    // Stop after paid places; remaining zero-prize finishes add nothing to EV.
    if (place >= prizes.size() || probability[mask] == 0.0)
      continue;
    std::int64_t remaining_chips = 0;
    for (std::size_t player = 0; player < stacks.size(); ++player)
      if (!(mask & (1u << player)))
        remaining_chips += stacks[player];
    // At most eight int stacks are summed in int64_t. Because a paid position
    // still remains and all stacks are positive, this denominator is nonzero.
    for (std::size_t player = 0; player < stacks.size(); ++player) {
      const unsigned bit = 1u << player;
      if (mask & bit)
        continue; // A player cannot occupy two finishing positions.
      const double chance =
          probability[mask] * (static_cast<double>(stacks[player]) /
                               static_cast<double>(remaining_chips));
      // This path assigns the current prize to this player. For example, a
      // 0.25 probability of receiving a 500-unit prize contributes 125 units.
      expected[player] += chance * static_cast<double>(prizes[place]);
      // Earlier winners are removed for the next position. Several distinct
      // orders can reach this new group, so add their probabilities together.
      probability[mask | bit] += chance;
    }
  }
  // Values retain the input order. No cards, game state, RNG, or money are
  // modified; the returned doubles are estimates in the supplied prize units.
  return expected;
}
} // namespace poker
