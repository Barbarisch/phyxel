#include "core/Spline.h"

#include <algorithm>

namespace Phyxel {

Spline::Spline(std::vector<Point> points) : m_points(std::move(points)) {
    std::stable_sort(m_points.begin(), m_points.end(),
                     [](const Point& a, const Point& b) { return a.x < b.x; });
}

Spline Spline::ramp(float x0, float y0, float x1, float y1) {
    return Spline({{x0, y0}, {x1, y1}});
}

float Spline::eval(float x) const {
    if (m_points.empty()) return 0.0f;
    if (x <= m_points.front().x) return m_points.front().y;
    if (x >= m_points.back().x) return m_points.back().y;

    // Find the segment [i, i+1] that brackets x (points are sorted, small counts → linear scan).
    size_t i = 0;
    while (i + 1 < m_points.size() && x >= m_points[i + 1].x) ++i;
    const Point& a = m_points[i];
    const Point& b = m_points[i + 1];

    const float span = b.x - a.x;
    if (span <= 0.0f) return a.y;   // coincident x guard (sorted, so degenerate segment → left value)
    float t = (x - a.x) / span;
    float s = t * t * (3.0f - 2.0f * t);   // smoothstep: flat + smooth at each knot, no overshoot
    return a.y + (b.y - a.y) * s;
}

}  // namespace Phyxel
