// Deterministic placeholder policy for automatic games and engine throughput
// checks.

#ifndef POKER_SIMPLE_AI_DECISION_PROVIDER_HPP
#define POKER_SIMPLE_AI_DECISION_PROVIDER_HPP

// The simple AI implements the same interface future human/AI providers use.
#include "decision/DecisionProvider.hpp"

namespace poker {

// A deterministic placeholder AI for running games automatically. It checks
// when possible, calls inexpensive bets, and otherwise folds. It never raises
// or evaluates hand strength. final prevents further inheritance from this AI.
class SimpleAIDecisionProvider final : public DecisionProvider {
public:
  // override ensures this matches DecisionProvider's choose_action signature.
  PlayerDecision choose_action(const PlayerView &view) override;
};

} // namespace poker

#endif // POKER_SIMPLE_AI_DECISION_PROVIDER_HPP
