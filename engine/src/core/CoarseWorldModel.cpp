#include "core/CoarseWorldModel.h"

#include <cmath>

namespace Phyxel {

CoarseWorldModel::CoarseWorldModel(SourceFunc source, float cellSize)
    : m_source(std::move(source)), m_cellSize(cellSize > 0.0f ? cellSize : 1.0f) {}

CoarseSample CoarseWorldModel::sample(float worldX, float worldZ) const {
    // Map the world column into coarse-cell space and find the enclosing cell's corners.
    const float gx = worldX / m_cellSize;
    const float gz = worldZ / m_cellSize;
    const float cx0 = std::floor(gx);
    const float cz0 = std::floor(gz);
    const float tx = gx - cx0;   // [0,1) fractional position within the cell
    const float tz = gz - cz0;

    // Corner world positions. Adjacent cells share corners, so evaluating the source at
    // these integer-cell coordinates makes the interpolation seamless across borders.
    const float x0 = cx0 * m_cellSize;
    const float x1 = (cx0 + 1.0f) * m_cellSize;
    const float z0 = cz0 * m_cellSize;
    const float z1 = (cz0 + 1.0f) * m_cellSize;

    const CoarseSample s00 = m_source(x0, z0);
    const CoarseSample s10 = m_source(x1, z0);
    const CoarseSample s01 = m_source(x0, z1);
    const CoarseSample s11 = m_source(x1, z1);

    auto bilerp = [tx, tz](float v00, float v10, float v01, float v11) {
        const float a = v00 + (v10 - v00) * tx;   // interpolate along X at z0
        const float b = v01 + (v11 - v01) * tx;   // interpolate along X at z1
        return a + (b - a) * tz;                  // interpolate along Z
    };

    CoarseSample out;
    out.baseHeight      = bilerp(s00.baseHeight,      s10.baseHeight,      s01.baseHeight,      s11.baseHeight);
    out.temperature     = bilerp(s00.temperature,     s10.temperature,     s01.temperature,     s11.temperature);
    out.moisture        = bilerp(s00.moisture,        s10.moisture,        s01.moisture,        s11.moisture);
    out.continentalness = bilerp(s00.continentalness, s10.continentalness, s01.continentalness, s11.continentalness);
    return out;
}

} // namespace Phyxel
