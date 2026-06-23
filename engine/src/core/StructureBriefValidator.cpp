#include "core/StructureBriefValidator.h"

namespace Phyxel {
namespace Core {

ValidationReport StructureBriefValidator::validate(const StructureBrief& brief,
                                                   const StructureBriefSchema& schema) {
    ValidationReport r;

    // 1) Every blocking field must be set.
    for (const BriefField* f : schema.blockingFields())
        if (!brief.has(f->id))
            r.addError("blocking_missing",
                       "required field '" + f->id + "' (" + f->label + ") is not set", f->id);

    // 2) Every set value must be grounded (sourced + confirmed); enums in-range.
    for (const auto& [id, bv] : brief.fields()) {
        if (!bv.sourced())
            r.addError("unsourced", "field '" + id + "' has no source — every value must be "
                       "user-provided or a confirmed citation", id);
        else if (!bv.confirmed)
            r.addError("unconfirmed", "field '" + id + "' has a source but is not confirmed", id);

        const BriefField* f = schema.field(id);
        if (f && f->type == "enum" && !f->options.empty() && bv.value.is_string()) {
            const std::string v = bv.value.get<std::string>();
            bool inRange = false;
            for (const auto& o : f->options) if (o == v) { inRange = true; break; }
            if (!inRange)
                r.addWarning("enum_unknown",
                             "value '" + v + "' is not one of field '" + id + "'s options", id);
        }
    }
    return r;
}

} // namespace Core
} // namespace Phyxel
