// Adapt terminal input to the same decision interface used by AI policies.
#pragma once
#include "decision/DecisionProvider.hpp"
namespace poker {
class TerminalUI;
class HumanDecisionProvider final : public DecisionProvider {
public:
  // The UI is borrowed and must outlive this provider.
  explicit HumanDecisionProvider(TerminalUI &terminal_ui);
  PlayerDecision choose_action(const PlayerView &view) override;

private:
  TerminalUI &terminal_ui_;
};
} // namespace poker
