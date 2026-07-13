#pragma once

#include <string>

namespace Phyxel {
namespace Core {

// ============================================================================
// Uuid — RFC-4122 version-4 (random) identifier utilities.
//
// Stable, globally-unique identifiers for world objects / structures / entities
// / item instances. Stored and compared as canonical lowercase 8-4-4-4-12
// strings, because everything the engine persists is JSON (nlohmann) or SQLite
// TEXT and every id map is already keyed by std::string — a 128-bit struct would
// only add conversions at every boundary.
//
// The strictness of isValid() is load-bearing: it validates the v4 version nibble
// and the RFC-4122 variant bits, so a legacy id ("chair_3", "entity_7", "player")
// can never be mistaken for a uuid. That is what lets the "resolve by either the
// legacy id OR the uuid" helpers disambiguate unambiguously.
// ============================================================================
namespace Uuid {

/// Generate a fresh RFC-4122 v4 UUID (122 random bits) as a canonical lowercase
/// string, e.g. "550e8400-e29b-41d4-a716-446655440000".
/// Thread-safe and lock-free (per-thread RNG) — safe to call while holding a
/// registry mutex (e.g. inside PlacedObjectManager::registerStructure).
std::string generate();

/// Strictly validate canonical 8-4-4-4-12 form AND the v4 version nibble (index
/// 14 == '4') AND the RFC-4122 variant nibble (index 19 in {8,9,a,b}). Lowercase
/// hex only. Returns false for legacy ids and for the nil UUID.
bool isValid(const std::string& s);

/// The nil UUID "00000000-0000-0000-0000-000000000000" (a sentinel — note it is
/// NOT isValid(), since its version nibble is 0, not 4).
const std::string& nil();

/// True for an empty string or the nil UUID.
bool isNilOrEmpty(const std::string& s);

} // namespace Uuid
} // namespace Core
} // namespace Phyxel
