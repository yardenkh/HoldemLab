// Binary lookup-table layout shared by the offline generator and runtime
// loader.

#pragma once

#include <array>
#include <cstdint>

namespace poker::fast_eval {

// Shared constants describe the binary file written by GenerateFastHandTables
// and read by FastHandEvaluator. Changing its layout requires a version change.

// The additive rank hash is collision-free for every legal 7-card rank
// multiset (13 ranks, each occurring 0..4 times, total count exactly 7).
// The largest legal sum is 7,825,759, so a uint16_t direct-address table
// occupies just under 15 MiB.
inline constexpr std::array<std::uint32_t, 13> kRankHash = {
    0u,    1u,     5u,     22u,     98u,     453u,    2031u,
    8698u, 22854u, 83661u, 262349u, 636345u, 1479181u};

inline constexpr std::uint32_t kNonFlushTableSize = 7'825'760u;
// A suit has 13 possible ranks, so its mask has 2^13 = 8,192 possible values.
inline constexpr std::uint32_t kFlushTableSize = 1u << 13;
inline constexpr std::uint32_t kDenseScoreCount = 7'462u;
// Entry zero is reserved; valid dense scores occupy positions 1 through 7,462.
inline constexpr std::uint32_t kPackedScoreCount = kDenseScoreCount + 1u;
inline constexpr std::uint32_t kTableVersion = 1u;
inline constexpr std::uint32_t kEndianMarker = 0x01020304u;

// Small descriptor at the start of the file, followed by non-flush, flush and
// packed-score arrays in that order. endian_marker detects a byte-order
// mismatch.
struct TableFileHeader {
  std::array<char, 8> magic{'P', 'K', 'R', 'E', 'V', 'A', 'L', '1'};
  std::uint32_t version = kTableVersion;
  std::uint32_t endian_marker = kEndianMarker;
  std::uint32_t non_flush_entries = kNonFlushTableSize;
  std::uint32_t flush_entries = kFlushTableSize;
  std::uint32_t packed_score_entries = kPackedScoreCount;
};

} // namespace poker::fast_eval
