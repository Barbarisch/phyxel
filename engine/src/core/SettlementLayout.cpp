#include "core/SettlementLayout.h"

namespace Phyxel {
namespace Core {

SettlementLayout subdividePlots(int W, int D, int cols, int rows, int streetWidth, int minPlot) {
    SettlementLayout out;
    if (cols < 1 || rows < 1 || streetWidth < 0 || W <= 0 || D <= 0) return out;

    // Reserve (cols+1) vertical street bands + (rows+1) horizontal bands; plots fill the rest.
    const int plotW = (W - (cols + 1) * streetWidth) / cols;
    const int plotD = (D - (rows + 1) * streetWidth) / rows;
    if (plotW < minPlot || plotD < minPlot) return out;   // can't fit -> caller reduces density

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            Plot p;
            p.col = c; p.row = r;
            p.rect.x = streetWidth + c * (plotW + streetWidth);   // inset by a street, gap per column
            p.rect.z = streetWidth + r * (plotD + streetWidth);
            p.rect.w = plotW;
            p.rect.d = plotD;
            out.plots.push_back(p);
        }
    }
    // street corridors: vertical bands (full D) at each x-gap, horizontal bands (full W) at each z-gap
    for (int c = 0; c <= cols; ++c) {
        Rect s; s.x = c * (plotW + streetWidth); s.z = 0; s.w = streetWidth; s.d = D;
        out.streets.push_back(s);
    }
    for (int r = 0; r <= rows; ++r) {
        Rect s; s.x = 0; s.z = r * (plotD + streetWidth); s.w = W; s.d = streetWidth;
        out.streets.push_back(s);
    }
    return out;
}

std::vector<PlacedBuilding> populatePlots(const SettlementLayout& layout, int setback,
                                          int minBuilding, const std::string& typology) {
    std::vector<PlacedBuilding> out;
    if (setback < 0) return out;
    for (size_t i = 0; i < layout.plots.size(); ++i) {
        const Rect& plot = layout.plots[i].rect;
        Rect fp;
        fp.x = plot.x + setback;       // inset by the yard on every side
        fp.z = plot.z + setback;
        fp.w = plot.w - 2 * setback;
        fp.d = plot.d - 2 * setback;
        if (fp.w < minBuilding || fp.d < minBuilding) continue;   // too small for a building + yard
        PlacedBuilding b;
        b.plotIndex = static_cast<int>(i);
        b.footprint = fp;
        b.typology = typology;
        out.push_back(b);
    }
    return out;
}

} // namespace Core
} // namespace Phyxel
