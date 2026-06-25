#pragma once

// ============================================================================
// StructureBriefValidator — the engine-resident intake gate (docs/structure-generation/StructureBrief.md).
//
// Validates a filled StructureBrief against the StructureBriefSchema and returns a
// ValidationReport: every BLOCKING field must be set; every set value must carry a
// non-empty `source` and `confirmed:true`; enum values must be in the schema's
// options. The validator is STRICT (it reports real gate failures as errors); the
// build POLICY is warn-but-allow — callers log the report loudly and proceed, so
// nothing is silently ungrounded but dev/test flow isn't bricked.
// ============================================================================

#include "core/StructureBrief.h"
#include "core/StructureBriefSchema.h"
#include "core/ValidationReport.h"

namespace Phyxel {
namespace Core {

class StructureBriefValidator {
public:
    static ValidationReport validate(const StructureBrief& brief,
                                     const StructureBriefSchema& schema);
};

} // namespace Core
} // namespace Phyxel
