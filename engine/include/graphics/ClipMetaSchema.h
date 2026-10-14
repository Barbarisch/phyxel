#pragma once
// ============================================================================
// ClipMetaSchema — THE typed schema for `# clip_meta:` header lines (A3 item 1,
// docs/AnimationSystemV3Plan.md §4 A3).
//
// Before this, AnimatedVoxelCharacter::applyClipMetaFromFile ran every value through
// std::stof and string-valued keys survived only via a tool-only skip list. Factor
// coordinates (gait/state/grip/load/condition/mood/role/mask/additive) are strings, so the
// schema is now a table — loaded from resources/anim/clip_meta_schema.json, the same file
// tools/anim_pipeline/clip_meta_schema.py reads, with a builtin fallback for the legacy
// numeric keys so a packaged game without the file degrades to today's behaviour.
//
// Rules: unknown key → Issue::UnknownKey (callers WARN once); wrong type / value outside an
// enum → WrongType / BadEnum (lint ERROR, runtime WARN + ignore). Factor keys land in
// AnimationClip::factors; absent = "any".
// ============================================================================
#include "graphics/Animation.h"

#include <map>
#include <string>
#include <vector>

namespace Phyxel::ClipMeta {

enum class ValueType { Float, Bool, String, Enum };

struct KeySpec {
    ValueType type = ValueType::Float;
    std::vector<std::string> values;   // Enum only
    bool toolOnly = false;             // authored by tools, never read by the runtime
    bool factor   = false;             // composition coordinate → AnimationClip::factors
    std::string runtimeField;          // AnimationClip member a numeric key drives ("" = none)
    bool alias    = false;             // releaseFrame → hitFrameFraction (explicit wins)
};

/// The schema, loaded once. `schemaLoadedFromFile()` says whether the JSON was found.
const std::map<std::string, KeySpec>& schema();
bool schemaLoadedFromFile();
/// Path the loader looks at (relative to the working directory).
const char* schemaPath();

struct Issue {
    enum Kind { UnknownKey, WrongType, BadEnum } kind;
    std::string key, value, message;
};

struct Parsed {
    std::string type;                              // the `type=` value, if present
    std::map<std::string, float>       numbers;    // typed float/bool keys (bool as 0/1)
    std::map<std::string, std::string> factors;    // factor keys, normalized (bool → "0"/"1")
    std::map<std::string, std::string> strings;    // other string keys (tool-only families…)
    std::vector<Issue> issues;
};

/// Parse the `key=value ...` tail of a clip_meta line (everything after the clip name).
Parsed parseFields(const std::string& kvText);

/// Apply a parsed line to a clip: numeric runtime fields, clipType, factors.
void applyToClip(AnimationClip& clip, const Parsed& parsed);

/// Validate every `# clip_meta:` header line of a .anim file without loading the clips.
/// Returns one entry per issue as "<clip>: <message>".
std::vector<std::string> validateFile(const std::string& animPath);

// ---- factor selection (composition = nearest base + every matching layer) -------------

struct Factors {
    std::string gait      = "biped";
    std::string state;                 // FSM state, lower-case; required
    std::string grip      = "empty";
    std::string load      = "none";
    std::string condition = "fresh";
    std::string mood      = "neutral";
};

/// -1 when the clip is not a base for these factors (role != base, state mismatch, or a
/// declared coordinate that differs); otherwise the number of declared coordinates that
/// match exactly (higher = nearer).
int baseMatchScore(const AnimationClip& clip, const Factors& f);
/// True when the clip is role=layer and every coordinate it declares matches.
bool layerMatches(const AnimationClip& clip, const Factors& f);

struct Composition {
    int base = -1;               // index into the clip vector, -1 = no base for this state
    std::vector<int> layers;     // indices, in clip order
};
Composition selectComposition(const std::vector<AnimationClip>& clips, const Factors& f);

} // namespace Phyxel::ClipMeta
