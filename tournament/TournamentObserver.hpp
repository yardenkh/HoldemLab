// Tournament events are separate from dealing/betting events in GameObserver.
#pragma once
#include "table/Table.hpp"
#include "tournament/TournamentConfig.hpp"
#include "tournament/TournamentResult.hpp"
namespace poker {
/** Optional borrowed listener. Callbacks run on the tournament's thread and
 * should not throw or modify the tournament. Headless runs need no observer. */
class TournamentObserver {
public:
  virtual ~TournamentObserver() = default;
  virtual void on_tournament_started(int /*players*/,
                                     const TournamentConfig &) {}
  virtual void on_blind_level_started(std::size_t /*level*/, const BlindLevel &,
                                      std::uint64_t /*completed_hands*/) {}
  // Unlike GameObserver's hand-finish event, rankings and prizes are settled
  // here.
  virtual void on_tournament_hand_finished(const Table &,
                                           const TournamentResult &) {}
};
} // namespace poker
