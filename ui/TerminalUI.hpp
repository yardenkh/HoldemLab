// Terminal presentation is separate from poker rules and headless simulations.
#pragma once
#include "game/GameObserver.hpp"
#include "view/PlayerView.hpp"
#include <iosfwd>
#include <string>
namespace poker {
class TerminalUI final : public GameObserver {
public:
  // Real terminals get a refreshed dashboard. Redirected output stays plain.
  TerminalUI();
  // Streams are borrowed. Tests can explicitly enable dashboard mode to check
  // redraws and pauses without requiring an actual terminal.
  TerminalUI(std::istream &input, std::ostream &output,
             bool interactive = false);
  PlayerDecision request_decision(const PlayerView &view);
  void print_player_view(const PlayerView &view) const;
  void on_hand_started(int number, const Table &, const PotManager &) override;
  void on_stage_started(PokerStage, const Table &, const PotManager &) override;
  void on_player_action(int id, const PlayerDecision &, const Table &,
                        const PotManager &, const BettingRoundState &) override;
  void on_showdown(const Table &, const PotManager &,
                   const std::array<HandScore, MAX_PLAYERS> &) override;
  void on_hand_finished(const Table &,
                        const std::array<int, MAX_PLAYERS> &) override;

private:
  std::istream &input_;
  std::ostream &output_;
  bool interactive_;
  int hand_number_ = 0;
  int human_id_ = 0;
  PokerStage stage_ = PokerStage::PreFlop;
  // Only display data is cached here. The engine remains the source of truth
  // for stacks, turns, cards and legal actions.
  std::array<std::string, MAX_PLAYERS> last_actions_{};
  std::array<int, MAX_PLAYERS> starting_stacks_{};
  std::string showdown_text_;
  void print_dashboard(const PlayerView &view, bool finished) const;
  void print_table(const PlayerView &view) const;
  void print_choices(const PlayerView &view) const;
  bool compact_display() const;
};
} // namespace poker
