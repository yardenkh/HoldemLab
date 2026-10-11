// Human input chooses requests; the game remains responsible for execution.
#include "decision/HumanDecisionProvider.hpp"
#include "ui/TerminalUI.hpp"
namespace poker {
HumanDecisionProvider::HumanDecisionProvider(TerminalUI &terminal_ui)
    : terminal_ui_(terminal_ui) {}
PlayerDecision HumanDecisionProvider::choose_action(const PlayerView &view) {
  // Delegate input and retry handling to the terminal adapter.
  return terminal_ui_.request_decision(view);
}
} // namespace poker
