// Trusted engine-only mutation path after validation; external callers use
// execute_decision.

#pragma once
#include "betting/Betting.hpp"

namespace poker::detail {
// Internal engine operation, not an input boundary. The caller MUST validate
// this decision against the same state immediately before calling. Keeping
// the mutation in one function prevents the fast and public paths diverging.
void apply_validated_decision(Table &, PotManager &, BettingRoundState &, int,
                              const PlayerDecision &);
} // namespace poker::detail
