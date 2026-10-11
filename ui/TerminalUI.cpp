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
    return "fold";
  case ActionType::Check:
    return "check";
  case ActionType::Call:
    return "call";
  case ActionType::RaiseTo:
    return "raise to " + std::to_string(d.raise_to);
  case ActionType::AllIn:
    return "all-in";
  }
  return "unknown";
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
void TerminalUI::print_recent_actions() const {
  const bool compact = compact_display();
  output_ << (compact ? "RECENT ACTIONS (this hand)\n"
                      : "\nRECENT ACTIONS (this hand)\n");
  if (events_.empty())
    output_ << "  Waiting for the first action.\n";
  // Bound the visible history so the decision area stays close to the table.
  const std::size_t limit = compact ? 2 : 6;
  const auto first = events_.size() > limit ? events_.size() - limit : 0;
  for (std::size_t i = first; i < events_.size(); ++i)
    output_ << "  " << events_[i] << '\n';
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
  output_ << (compact ? "\n COMMUNITY CARDS\n"
                      : "\n\n                        COMMUNITY CARDS\n");
  card_boxes(output_, v.community_cards, v.community_card_count, compact);
  output_ << " POT: " << v.pot_amount
          << " chips   |   ROUND BET: " << v.current_bet
          << "\n---------------------------------------------------------------"
             "-----\n";
  // Two columns keep even an eight-player table compact. Opponent hole cards
  // are never read here; the restricted PlayerView does not contain them.
  if (compact)
    output_ << " SEATS: stack / paid this round | D=dealer, SB/BB=blinds\n";
  for (int row = 0; row < v.num_players; row += 2) {
    for (int id = row; id < std::min(row + 2, v.num_players); ++id) {
      std::ostringstream seat;
      if (compact) {
        seat << (id == v.player_id ? "> YOU P" : "  P") << id;
        if (id == v.dealer_id)
          seat << " D";
        if (id == v.small_blind_id)
          seat << " SB";
        if (id == v.big_blind_id)
          seat << " BB";
        seat << " | " << v.player_stacks[id] << " / "
             << v.round_contributions[id]
             << (!v.players_in_hand[id] ? " OUT"
                 : v.players_all_in[id] ? " ALL-IN"
                                        : " IN");
        output_ << std::left << std::setw(35) << seat.str();
        continue;
      }
      seat << (id == v.player_id ? "> YOU (P" : "  Player ") << id;
      if (id == v.player_id)
        seat << ')';
      if (id == v.dealer_id)
        seat << " D";
      if (id == v.small_blind_id)
        seat << " SB";
      if (id == v.big_blind_id)
        seat << " BB";
      seat << " | " << v.player_stacks[id] << " chips";
      output_ << std::left << std::setw(35) << seat.str();
    }
    output_ << '\n';
    if (compact)
      continue;
    for (int id = row; id < std::min(row + 2, v.num_players); ++id) {
      std::ostringstream detail;
      const char *status = !v.players_in_hand[id] ? "OUT"
                           : v.players_all_in[id] ? "ALL-IN"
                                                  : "IN HAND";
      detail << "  " << status << " | paid " << v.round_contributions[id];
      if (!last_actions_[id].empty())
        detail << " | " << last_actions_[id];
      output_ << std::left << std::setw(35) << detail.str();
    }
    output_ << '\n';
  }
  output_ << "-----------------------------------------------------------------"
             "---\n"
          << " YOUR CARDS (player " << v.player_id << ")\n";
  card_boxes(output_, v.hole_cards, v.hole_card_count, compact);
  output_ << " YOUR LAST MOVE: "
          << (your_last_move_.empty() ? "None yet this hand" : your_last_move_)
          << '\n';
  print_recent_actions();
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
  events_.clear();
  last_actions_.fill({});
  your_last_move_.clear();
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
  events_.push_back(event);
  if (!interactive_)
    output_ << event << " | Pot: " << pots.total_pot_amount() << '\n';
  // The next decision redraws the dashboard with the new public board. AI-only
  // runouts do not flash past the player; all stage transitions stay in
  // history.
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
  events_.push_back(event);
  if (id == human_id_)
    your_last_move_ = poker_stage_to_string(stage_) + " - " + label;
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
  if (interactive_) {
    output_ << "\n Press ENTER to continue > " << std::flush;
    std::string line;
    if (!std::getline(input_, line))
      throw std::runtime_error("Terminal input closed; game stopped.");
  }
}
} // namespace poker
