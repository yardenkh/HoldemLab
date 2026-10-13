// Tournament progression uses settled hand results; all poker rules stay in
// PokerGame.
#include "tournament/Tournament.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace poker {
GameConfig Tournament::validate_config(int num_players,
                                       const TournamentConfig &config) {
  if (num_players < 2 || num_players > MAX_PLAYERS ||
      config.starting_chips <= 0 || config.blind_levels.empty() ||
      config.prizes.empty() ||
      config.prizes.size() > static_cast<std::size_t>(num_players))
    throw std::invalid_argument(
        "Invalid tournament seats, stacks, levels or prizes");
  int previous_small = 0, previous_big = 0;
  for (const auto &level : config.blind_levels) {
    // This tournament uses positive, strictly ordered blinds that never
    // decrease. PokerGame also needs room to add the current bet and minimum
    // raise in int.
    if (level.small_blind <= 0 || level.big_blind <= level.small_blind ||
        level.small_blind < previous_small || level.big_blind < previous_big ||
        level.hands_per_level <= 0 ||
        level.big_blind > std::numeric_limits<int>::max() / 2)
      throw std::invalid_argument("Invalid tournament blind schedule");
    previous_small = level.small_blind;
    previous_big = level.big_blind;
  }
  std::int64_t total_prizes = 0;
  for (std::size_t i = 0; i < config.prizes.size(); ++i) {
    const auto prize = config.prizes[i];
    // Bound the TOTAL as well as each entry, so tie-pool addition cannot
    // overflow.
    if (prize < 0 || (i > 0 && prize > config.prizes[i - 1]) ||
        prize > std::numeric_limits<std::int64_t>::max() - total_prizes)
      throw std::invalid_argument("Invalid or overflowing tournament prizes");
    total_prizes += prize;
  }
  const auto &first = config.blind_levels.front();
  return {config.starting_chips, first.small_blind, first.big_blind};
}

Tournament::Tournament(int num_players, const TournamentConfig &config,
                       std::uint32_t seed, const FastHandEvaluator &evaluator)
    : config_(config), game_(num_players, validate_config(num_players, config),
                             seed, evaluator) {
  result_.num_players = result_.players_remaining = num_players;
}
void Tournament::set_decision_provider(int id, DecisionProvider &provider) {
  game_.set_decision_provider(id, provider);
}
void Tournament::set_observer(GameObserver *observer) {
  game_.set_observer(observer);
}
void Tournament::set_tournament_observer(TournamentObserver *observer) {
  tournament_observer_ = observer;
}
void Tournament::reset(std::uint32_t seed) {
  // PokerGame preserves its borrowed providers/observer and reusable
  // allocations.
  game_.reset(seed);
  game_.set_blinds(config_.blind_levels[0].small_blind,
                   config_.blind_levels[0].big_blind);
  result_ = {};
  result_.num_players = result_.players_remaining = game_.table().num_players;
  level_ = 0;
  hands_at_level_ = 0;
  hand_pending_ = started_ = false;
  starting_stacks_.fill(0);
}
std::int64_t Tournament::prize_for_place(int place) const {
  // Prize arrays are zero-based, while human finishing positions start at one.
  return place <= static_cast<int>(config_.prizes.size())
             ? config_.prizes[place - 1]
             : 0;
}
bool Tournament::play_one_hand() {
  if (finished())
    return false;
  if (!hand_pending_) {
    if (!started_) {
      started_ = true;
      if (tournament_observer_) {
        tournament_observer_->on_tournament_started(result_.num_players,
                                                    config_);
        tournament_observer_->on_blind_level_started(level_, blind_level(),
                                                     result_.hands_played);
      }
    }
    // Move to the next level only before a new hand. The last level is held
    // indefinitely, even after its configured duration has elapsed.
    if (level_ + 1 < config_.blind_levels.size() &&
        hands_at_level_ >=
            static_cast<std::uint64_t>(blind_level().hands_per_level)) {
      ++level_;
      hands_at_level_ = 0;
      if (tournament_observer_)
        tournament_observer_->on_blind_level_started(level_, blind_level(),
                                                     result_.hands_played);
    }
    game_.set_blinds(blind_level().small_blind, blind_level().big_blind);
    for (int id = 0; id < result_.num_players; ++id)
      starting_stacks_[id] = game_.table().players[id].cash;
    hand_pending_ = true;
  }
  // If a provider throws, retain hand_pending_ and pre-blind stacks. A retry
  // resumes the engine's hand rather than reapplying blinds or changing ranks.
  game_.play_one_hand();
  ++result_.hands_played;
  ++hands_at_level_;
  record_finished_hand();
  hand_pending_ = false;
  if (tournament_observer_)
    tournament_observer_->on_tournament_hand_finished(game_.table(), result_);
  return true;
}
void Tournament::record_finished_hand() {
  std::array<int, MAX_PLAYERS> eliminated{};
  int count = 0;
  for (int id = 0; id < result_.num_players; ++id)
    if (starting_stacks_[id] > 0 && game_.table().players[id].cash == 0)
      eliminated[count++] = id;
  // Largest starting stack finishes highest among this hand's eliminations.
  // Seat ID only orders equal stacks for indivisible money units, not ranking.
  std::sort(eliminated.begin(), eliminated.begin() + count, [&](int a, int b) {
    return starting_stacks_[a] != starting_stacks_[b]
               ? starting_stacks_[a] > starting_stacks_[b]
               : a < b;
  });
  const int survivors = result_.players_remaining - count;
  for (int first = 0; first < count;) {
    int end = first + 1;
    while (end < count && starting_stacks_[eliminated[end]] ==
                              starting_stacks_[eliminated[first]])
      ++end;
    const int first_place = survivors + first + 1;
    const int last_place = survivors + end;
    std::int64_t pool = 0;
    for (int place = first_place; place <= last_place; ++place)
      pool += prize_for_place(place);
    const int tied = end - first;
    const auto share = pool / tied;
    const auto remainder = pool % tied;
    for (int index = first; index < end; ++index) {
      // Record the whole tied position interval for each tied player. Dividing
      // integer prize units preserves the pool; lower seat IDs get any
      // remainder.
      auto &standing = result_.players[eliminated[index]];
      standing.first_place = first_place;
      standing.last_place = last_place;
      standing.prize = share + (index - first < remainder ? 1 : 0);
      standing.eliminated_on_hand = result_.hands_played;
    }
    first = end;
  }
  result_.players_remaining = survivors;
  if (survivors == 1) {
    result_.winner_id = game_.winner_id();
    result_.players[result_.winner_id] = {1, 1, prize_for_place(1), 0};
  }
}
void Tournament::run(int max_hands) {
  int played = 0;
  while (!finished() && (max_hands <= 0 || played < max_hands)) {
    play_one_hand();
    if (max_hands > 0)
      ++played;
  }
}
} // namespace poker
