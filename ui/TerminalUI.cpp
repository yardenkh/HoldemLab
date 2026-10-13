// Render a stable poker dashboard; parse requests through the engine validator.
#include "ui/TerminalUI.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <io.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif
namespace poker {
namespace {
// Avoid control sequences and extra pauses in files, pipelines and tests.
bool terminal_connected() {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
  return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#endif
}
// Cards occupy three rows. Unrevealed board slots show dots rather than the
// default Card value; only entries below count are real, visible cards.
template <std::size_t N>
void card_boxes(std::ostream &out, const std::array<Card, N> &cards, int count,
                bool compact = false) {
  // Short terminals use single-row card tiles so the action prompt stays
  // visible.
  if (compact) {
    for (std::size_t i = 0; i < N; ++i)
      out << "[ "
          << (static_cast<int>(i) < count ? card_to_string(cards[i]) : "..")
          << " ] ";
    out << '\n';
    return;
  }
  for (std::size_t i = 0; i < N; ++i)
    out << "+-----+ ";
  out << '\n';
  for (std::size_t i = 0; i < N; ++i) {
    const auto label =
        static_cast<int>(i) < count ? card_to_string(cards[i]) : "..";
    out << "| " << std::left << std::setw(3) << label << " | ";
  }
  out << '\n';
  for (std::size_t i = 0; i < N; ++i)
    out << "+-----+ ";
  out << '\n';
}
std::string action_label(const PlayerDecision &d) {
  switch (d.action) {
  case ActionType::Fold:
    return "FOLD";
  case ActionType::Check:
    return "CHECK";
  case ActionType::Call:
    return "CALL";
  case ActionType::RaiseTo:
    return "RAISE " + std::to_string(d.raise_to);
  case ActionType::AllIn:
    return "ALL-IN";
  }
  return "UNKNOWN";
}
// Accept one complete command, including optional single-letter shortcuts.
// Malformed numbers and extra words are never partially accepted.
bool parse_decision(const std::string &line, PlayerDecision &decision) {
  std::istringstream input(line);
  std::string command, extra;
  if (!(input >> command))
    return false;
  decision.raise_to = 0;
  if (command == "fold" || command == "f")
    decision.action = ActionType::Fold;
  else if (command == "check" || command == "k")
    decision.action = ActionType::Check;
  else if (command == "call" || command == "c")
    decision.action = ActionType::Call;
  else if (command == "allin" || command == "a")
    decision.action = ActionType::AllIn;
  else if (command == "raise" || command == "r") {
    decision.action = ActionType::RaiseTo;
    if (!(input >> decision.raise_to))
      return false;
  } else
    return false;
  return !(input >> extra);
}
} // namespace
TerminalUI::TerminalUI()
    : TerminalUI(std::cin, std::cout, terminal_connected()) {}
TerminalUI::TerminalUI(std::istream &input, std::ostream &output,
                       bool interactive)
    : input_(input), output_(output), interactive_(interactive) {}
bool TerminalUI::compact_display() const {
  // Recheck the terminal size at each redraw, including after window resizing.
  // Stream tests keep the full layout; a normal 24-row console gets compact
  // tiles.
  if (!interactive_ || &output_ != &std::cout)
    return false;
#ifndef _WIN32
  winsize size{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0)
    return size.ws_row < 40;
#endif
  return true;
}
void TerminalUI::print_dashboard(const PlayerView &v, bool finished) const {
  const bool compact = compact_display();
  // Clear/home only on interactive terminals. Re-rendering one screen avoids
  // burying the current hand beneath an ever-growing list of events.
  if (interactive_)
    output_ << "\033[2J\033[H\033[1;36m";
  if (!compact)
    output_
        << "================================================================="
           "===\n";
  output_ << " TEXAS HOLD'EM   |   HAND " << hand_number_ << "   |   "
          << poker_stage_to_string(v.stage) << (finished ? " - RESULTS" : "")
          << "\n==============================================================="
             "=====\n";
  if (interactive_)
    output_ << "\033[0m";
  if (tournament_mode_)
    output_ << " TOURNAMENT | Level " << tournament_level_ + 1 << " | Blinds "
            << tournament_blinds_.small_blind << '/'
            << tournament_blinds_.big_blind
            << " | Remaining: " << tournament_remaining_ << '\n';
  // A stage path makes it clear where this decision fits within the hand.
  output_ << " ";
  for (const auto stage :
       {PokerStage::PreFlop, PokerStage::Flop, PokerStage::Turn,
        PokerStage::River, PokerStage::Showdown}) {
    if (stage == v.stage)
      output_ << '[';
    output_ << poker_stage_to_string(stage);
    if (stage == v.stage)
      output_ << ']';
    if (stage != PokerStage::Showdown)
      output_ << " > ";
  }
  output_ << '\n';
  print_table(v);
  output_ << " YOUR CARDS (player " << v.player_id << ")\n";
  card_boxes(output_, v.hole_cards, v.hole_card_count, compact);
}
void TerminalUI::print_table(const PlayerView &v) const {
  // Eight positions around the board, clockwise from the human at the bottom.
  // Fewer players use evenly spaced positions; player IDs retain table order.
  constexpr std::array<int, 8> rows{10, 8, 5, 2, 0, 2, 5, 8};
  constexpr std::array<int, 8> sides{0, -1, -1, -1, 0, 1, 1, 1};
  std::array<std::string, MAX_PLAYERS> names{}, details{};
  std::size_t seat_width = 22;
  for (int id = 0; id < v.num_players; ++id) {
    std::ostringstream name, detail;
    name << (id == v.player_id ? "> YOU P" : "P") << id;
    if (id == v.dealer_id)
      name << " D";
    if (id == v.small_blind_id)
      name << " SB";
    if (id == v.big_blind_id)
      name << " BB";
    // An observed action takes priority over generic hand status. A blind can
    // cause all-in before a decision; an eliminated seat never acts this hand.
    std::string action = last_actions_[id];
    if (action.empty()) {
      if (v.players_all_in[id])
        action = "ALL-IN";
      else if (!v.players_in_hand[id])
        action = v.player_stacks[id] == 0 ? "ELIMINATED" : "FOLD";
      else
        action = "WAIT";
    }
    detail << v.player_stacks[id] << " / " << v.round_contributions[id] << " "
           << action;
    names[id] = name.str();
    details[id] = detail.str();
    // Grow rather than truncate unusually large stacks or raise targets.
    seat_width = std::max({seat_width, names[id].size(), details[id].size()});
  }
  std::ostringstream cards, pot;
  for (int i = 0; i < MAX_COMMUNITY_CARDS; ++i) {
    const auto label = i < v.community_card_count
                           ? card_to_string(v.community_cards[i])
                           : "..";
    cards << "[" << std::right << std::setw(3) << label << "] ";
  }
  pot << "POT " << v.pot_amount << " | BET " << v.current_bet;
  const std::size_t centre_width =
      std::max<std::size_t>(34, pot.str().size() + 4);
  // Leave a space on each side of the felt so its border does not touch seats.
  const std::size_t width = 2 * seat_width + centre_width + 2;
  std::array<std::string, 12> canvas;
  canvas.fill(std::string(width, ' '));
  // Compose fixed coordinates before printing so seat text never collides
  // with another player or the central board, regardless of seat count.
  const auto put = [&](int row, std::size_t column, const std::string &text) {
    canvas[row].replace(column, text.size(), text);
  };
  const auto centre = [&](int row, const std::string &text) {
    put(row, (width - text.size()) / 2, text);
  };
  centre(4, "/" + std::string(centre_width - 2, '-') + "\\");
  centre(5, "|" + std::string(centre_width - 2, ' ') + "|");
  centre(5, cards.str());
  centre(6, "|" + std::string(centre_width - 2, ' ') + "|");
  centre(6, pot.str());
  centre(7, "\\" + std::string(centre_width - 2, '-') + "/");
  for (int relative = 0; relative < v.num_players; ++relative) {
    const int id = (v.player_id + relative) % v.num_players;
    // Integer rounding assigns distinct circular positions for 2..8 seats.
    const int slot = (relative * 8 + v.num_players / 2) / v.num_players;
    const std::size_t column = sides[slot] < 0   ? 0
                               : sides[slot] > 0 ? width - seat_width
                                                 : (width - seat_width) / 2;
    put(rows[slot], column, names[id]);
    put(rows[slot] + 1, column, details[id]);
  }
  for (const auto &line : canvas) {
    const auto end = line.find_last_not_of(' ');
    output_ << (end == std::string::npos ? "" : line.substr(0, end + 1))
            << '\n';
  }
}
void TerminalUI::print_choices(const PlayerView &v) const {
  const auto &a = v.legal_actions;
  if (interactive_)
    output_ << "\033[1;33m";
  if (!compact_display())
    output_ << '\n';
  output_ << "========================= YOUR TURN "
             "================================\n";
  if (interactive_)
    output_ << "\033[0m";
  output_ << " Stack: " << v.player_stacks[v.player_id] << " | To call: "
          << std::min(a.amount_to_call, v.player_stacks[v.player_id]) << '\n';
  // These buttons are only labels for choices supplied by the engine.
  if (a.can_fold)
    output_ << " [f] fold  ";
  if (a.can_check)
    output_ << " [k] check  ";
  if (a.can_call)
    output_ << " [c] call  ";
  if (a.can_all_in)
    output_ << " [a] allin  ";
  output_ << '\n';
  if (a.can_raise)
    output_ << " [r TOTAL] raise: " << a.min_raise_to << ".." << a.max_raise_to
            << " (total paid this round, not extra chips)\n";
}
void TerminalUI::print_player_view(const PlayerView &view) const {
  print_dashboard(view, false);
  print_choices(view);
}
PlayerDecision TerminalUI::request_decision(const PlayerView &view) {
  human_id_ = view.player_id;
  std::string error;
  for (;;) {
    // Invalid input redraws the same table, keeping errors beside the prompt.
    print_player_view(view);
    if (!error.empty())
      output_ << " ! " << error << '\n';
    output_ << " YOUR ACTION > " << std::flush;
    std::string line;
    if (!std::getline(input_, line))
      throw std::runtime_error("Terminal input closed; game stopped.");
    PlayerDecision decision;
    if (!parse_decision(line, decision)) {
      error = "Use fold, check, call, raise <total>, or allin (shortcuts shown "
              "above).";
      continue;
    }
    // Reuse betting validation instead of duplicating poker rules in the UI.
    if (!validate_decision(view.legal_actions, decision)) {
      error = "That action is not available. Use the displayed choices.";
      continue;
    }
    return decision;
  }
}
void TerminalUI::on_hand_started(int number, const Table &t,
                                 const PotManager &) {
  hand_number_ = number;
  stage_ = PokerStage::PreFlop;
  last_actions_.fill({});
  showdown_text_.clear();
  for (int id = 0; id < t.num_players; ++id)
    starting_stacks_[id] = t.players[id].cash;
  if (!interactive_)
    output_ << "\n=== Hand " << number << " ===\n";
}
void TerminalUI::on_stage_started(PokerStage stage, const Table &t,
                                  const PotManager &pots) {
  stage_ = stage;
  const auto event = "--- " + poker_stage_to_string(stage) + " ---";
  if (!interactive_)
    output_ << event << " | Pot: " << pots.total_pot_amount() << '\n';
  // The next decision redraws the circular table with the new public board.
  // Retain earlier actions so folded seats continue to show FOLD on later
  // streets.
  (void)t;
}
void TerminalUI::on_player_action(int id, const PlayerDecision &d,
                                  const Table &, const PotManager &,
                                  const BettingRoundState &) {
  const auto label = action_label(d);
  last_actions_[id] = label;
  const auto event =
      poker_stage_to_string(stage_) + " | " +
      (id == human_id_ ? "YOU" : "Player " + std::to_string(id)) + ": " + label;
  if (!interactive_)
    output_ << event << '\n';
}
void TerminalUI::on_showdown(const Table &t, const PotManager &pots,
                             const std::array<HandScore, MAX_PLAYERS> &scores) {
  // Buffer revealed hands for the result screen. Folded hands remain private.
  std::ostringstream result;
  for (int id = 0; id < t.num_players; ++id) {
    if (!t.players[id].in_hand)
      continue;
    result << " Player " << id << ": ";
    for (int c = 0; c < t.players[id].cards_in_hand; ++c)
      result << card_to_string(t.players[id].hand[c]) << ' ';
    result << "| " << hand_score_to_string(scores[id]) << '\n';
  }
  for (int i = 0; i < pots.num_pots(); ++i)
    result << (i ? " Side pot: " : " Main pot: ") << pots.pot(i).amount << '\n';
  showdown_text_ = result.str();
  if (!interactive_)
    output_ << showdown_text_;
}
void TerminalUI::on_hand_finished(const Table &t,
                                  const std::array<int, MAX_PLAYERS> &payouts) {
  // Pots are already settled here, so this count excludes actual eliminations
  // rather than temporarily empty stacks belonging to live all-in players.
  if (tournament_mode_)
    tournament_remaining_ = count_players_with_chips(t);
  if (interactive_) {
    // Construct display-only public state after payouts. No betting legality
    // is needed on this screen, and only the human's own cards are copied.
    PlayerView v;
    v.player_id = human_id_;
    v.num_players = t.num_players;
    v.stage = stage_;
    v.community_cards = t.community_cards;
    v.community_card_count = t.community_card_count;
    v.dealer_id = t.dealer_id;
    v.small_blind_id = t.small_blind_id;
    v.big_blind_id = t.big_blind_id;
    v.hole_cards = t.players[human_id_].hand;
    v.hole_card_count = t.players[human_id_].cards_in_hand;
    for (int id = 0; id < t.num_players; ++id) {
      v.player_stacks[id] = t.players[id].cash;
      v.players_in_hand[id] = t.players[id].in_hand;
      v.players_all_in[id] = t.players[id].all_in;
    }
    print_dashboard(v, true);
    output_ << showdown_text_;
  }
  output_ << "\n======================== HAND RESULT "
             "===============================\n"
          << " Hand finished. Chips received / net change / stack:\n";
  for (int id = 0; id < t.num_players; ++id) {
    // Gross payouts may include returned uncalled chips; net change makes the
    // actual gain or loss clear and uses stacks captured before blind payments.
    const int net = t.players[id].cash - starting_stacks_[id];
    output_ << (id == human_id_ ? " YOU" : " Player " + std::to_string(id))
            << " received " << payouts[id] << " | " << (net >= 0 ? "+" : "")
            << net << " | " << t.players[id].cash << " chips\n";
  }
  if (interactive_ && !tournament_mode_) {
    output_ << "\n Press ENTER to continue > " << std::flush;
    std::string line;
    if (!std::getline(input_, line))
      throw std::runtime_error("Terminal input closed; game stopped.");
  }
}
void TerminalUI::on_tournament_started(int players,
                                       const TournamentConfig &config) {
  tournament_mode_ = true;
  tournament_remaining_ = players;
  finish_announced_.fill(false);
  std::ostringstream prizes;
  for (std::size_t place = 0; place < config.prizes.size(); ++place)
    prizes << (place ? " | " : "") << place + 1 << ": " << config.prizes[place];
  tournament_prizes_ = prizes.str();
  output_ << "\nTOURNAMENT STARTED | " << players << " players | "
          << config.starting_chips
          << " chips each\nPrizes (units): " << tournament_prizes_ << "\n";
}
void TerminalUI::on_blind_level_started(std::size_t level,
                                        const BlindLevel &blinds,
                                        std::uint64_t completed_hands) {
  tournament_level_ = level;
  tournament_blinds_ = blinds;
  if (!interactive_)
    output_ << "BLIND LEVEL " << level + 1 << " | " << blinds.small_blind << '/'
            << blinds.big_blind << " | After " << completed_hands << " hands\n";
}
void TerminalUI::on_tournament_hand_finished(const Table &,
                                             const TournamentResult &result) {
  tournament_remaining_ = result.players_remaining;
  output_ << "\nTOURNAMENT STANDINGS | Remaining: " << result.players_remaining
          << " | Prizes (units): " << tournament_prizes_ << '\n';
  for (int id = 0; id < result.num_players; ++id) {
    const auto &finish = result.players[id];
    if (!finish.first_place || finish_announced_[id])
      continue;
    finish_announced_[id] = true;
    output_ << (id == human_id_ ? " YOU (P" : " Player ") << id;
    if (id == human_id_)
      output_ << ')';
    output_ << (id == result.winner_id ? " WINS" : " ELIMINATED")
            << " | Finish: " << finish.first_place;
    if (finish.last_place != finish.first_place)
      output_ << ".." << finish.last_place << " (tie)";
    output_ << " | Prize: " << finish.prize << " units\n";
  }
  if (result.winner_id >= 0) {
    output_ << "\n====================== FINAL TOURNAMENT RESULTS "
               "====================\n";
    // List finish intervals in order; ties retain their full position interval.
    for (int place = 1; place <= result.num_players; ++place)
      for (int id = 0; id < result.num_players; ++id) {
        const auto &finish = result.players[id];
        if (finish.first_place != place)
          continue;
        output_ << " Player " << id << " | Finish " << finish.first_place;
        if (finish.last_place != finish.first_place)
          output_ << ".." << finish.last_place;
        output_ << " | Prize " << finish.prize << " units\n";
      }
    output_ << " Tournament winner: player " << result.winner_id << '\n';
  }
  // GameObserver already printed cards and chip payouts. Pause only now, once
  // tournament ranks/prizes are also visible. No extra pause after the final
  // hand.
  if (interactive_ && result.winner_id < 0) {
    output_ << "\n Press ENTER for the next tournament hand > " << std::flush;
    std::string line;
    if (!std::getline(input_, line))
      throw std::runtime_error("Terminal input closed; game stopped.");
  }
}
} // namespace poker
