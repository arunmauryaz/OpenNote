#include "ob/Tools.h"
#include "ob/WhiteboardEngine.h"
#include "ob/CanvasCamera.h"
#include "ob/IRenderPipeline.h"
#include <cmath>
#include <algorithm>

namespace ob {

// ─── Centripetal Catmull-Rom Spline Interpolation (alpha = 0.5) ─────────────
// Prevents cusps, self-intersections, and wild loops during fast pen strokes.
[[maybe_unused]] static Vec2f centripetalCatmullRom(Vec2f p0, Vec2f p1, Vec2f p2, Vec2f p3,
                                    float pr1, float pr2, float t, float& outPressure)
{
    auto getT = [](float t, Vec2f p0, Vec2f p1) -> float {
        float dx = p1.x - p0.x;
        float dy = p1.y - p0.y;
        float len = std::sqrt(dx*dx + dy*dy);
        return t + std::pow(std::max(len, 0.0001f), 0.5f); // alpha = 0.5 centripetal
    };

    float t0 = 0.0f;
    float t1 = getT(t0, p0, p1);
    float t2 = getT(t1, p1, p2);
    float t3 = getT(t2, p2, p3);

    float globalT = t1 + t * (t2 - t1);

    auto interp = [](Vec2f v1, Vec2f v2, float t1, float t2, float t) -> Vec2f {
        if (std::abs(t2 - t1) < 0.0001f) return v1;
        float f = (t - t1) / (t2 - t1);
        return { v1.x + (v2.x - v1.x) * f, v1.y + (v2.y - v1.y) * f };
    };

    Vec2f a1 = interp(p0, p1, t0, t1, globalT);
    Vec2f a2 = interp(p1, p2, t1, t2, globalT);
    Vec2f a3 = interp(p2, p3, t2, t3, globalT);

    Vec2f b1 = interp(a1, a2, t0, t2, globalT);
    Vec2f b2 = interp(a2, a3, t1, t3, globalT);

    outPressure = pr1 + (pr2 - pr1) * t;
    return interp(b1, b2, t1, t2, globalT);
}

// ─── PenTool ──────────────────────────────────────────────────────────────────

PenTool::PenTool(WhiteboardEngine* engine)
    : m_engine(engine) {}

void PenTool::rebuildStroke() {
    m_currentStroke.points.clear();
    size_t n = m_rawPoints.size();
    if (n == 0) return;
    if (n == 1) {
        m_currentStroke.points.push_back(m_rawPoints[0]);
        return;
    }
    if (n == 2) {
        Vec2f p0 = {m_rawPoints[0].x, m_rawPoints[0].y};
        Vec2f p1 = {m_rawPoints[1].x, m_rawPoints[1].y};
        float pr0 = m_rawPoints[0].pressure;
        float pr1 = m_rawPoints[1].pressure;
        m_currentStroke.points.push_back({p0.x, p0.y, pr0});
        m_currentStroke.points.push_back({(p0.x + p1.x)*0.5f, (p0.y + p1.y)*0.5f, (pr0 + pr1)*0.5f});
        m_currentStroke.points.push_back({p1.x, p1.y, pr1});
        return;
    }

    m_currentStroke.points.reserve(n * 5 + 8);

    // 1. Initial point
    m_currentStroke.points.push_back(m_rawPoints[0]);

    // 2. First lead-in segment: from p0 to midpoint(p0, p1)
    Vec2f p0 = {m_rawPoints[0].x, m_rawPoints[0].y};
    Vec2f p1 = {m_rawPoints[1].x, m_rawPoints[1].y};
    Vec2f mid0 = {(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
    float pr0 = m_rawPoints[0].pressure;
    float pr1 = m_rawPoints[1].pressure;
    float prMid0 = (pr0 + pr1) * 0.5f;

    for (int s = 1; s <= 3; s++) {
        float t = (float)s / 3.0f;
        float invT = 1.0f - t;
        float bx = invT * invT * p0.x + 2.0f * invT * t * p0.x + t * t * mid0.x;
        float by = invT * invT * p0.y + 2.0f * invT * t * p0.y + t * t * mid0.y;
        float bp = invT * pr0 + t * prMid0;
        m_currentStroke.points.push_back({bx, by, bp});
    }

    // 3. Continuous C1-smooth Quadratic Bézier Midpoint Spline
    for (size_t i = 1; i + 1 < n; i++) {
        Vec2f prevPt = {m_rawPoints[i-1].x, m_rawPoints[i-1].y};
        Vec2f curPt  = {m_rawPoints[i].x,   m_rawPoints[i].y};
        Vec2f nextPt = {m_rawPoints[i+1].x, m_rawPoints[i+1].y};

        Vec2f startMid = {(prevPt.x + curPt.x) * 0.5f, (prevPt.y + curPt.y) * 0.5f};
        Vec2f endMid   = {(curPt.x + nextPt.x) * 0.5f, (curPt.y + nextPt.y) * 0.5f};

        float startPr = (m_rawPoints[i-1].pressure + m_rawPoints[i].pressure) * 0.5f;
        float curPr   = m_rawPoints[i].pressure;
        float endPr   = (m_rawPoints[i].pressure + m_rawPoints[i+1].pressure) * 0.5f;

        int steps = 4;
        for (int s = 1; s <= steps; s++) {
            float t = (float)s / (float)steps;
            float invT = 1.0f - t;
            float bx = invT * invT * startMid.x + 2.0f * invT * t * curPt.x + t * t * endMid.x;
            float by = invT * invT * startMid.y + 2.0f * invT * t * curPt.y + t * t * endMid.y;
            float bp = invT * invT * startPr + 2.0f * invT * t * curPr + t * t * endPr;
            m_currentStroke.points.push_back({bx, by, bp});
        }
    }

    // 4. Real-Time Leading Tip: Curve from last midpoint straight to live cursor point p_{n-1}
    Vec2f lastPt = {m_rawPoints[n-1].x, m_rawPoints[n-1].y};
    Vec2f prevPt = {m_rawPoints[n-2].x, m_rawPoints[n-2].y};
    Vec2f lastMid = {(prevPt.x + lastPt.x) * 0.5f, (prevPt.y + lastPt.y) * 0.5f};
    float lastPr = m_rawPoints[n-1].pressure;
    float lastMidPr = (m_rawPoints[n-2].pressure + lastPr) * 0.5f;

    for (int s = 1; s <= 3; s++) {
        float t = (float)s / 3.0f;
        float invT = 1.0f - t;
        float bx = invT * invT * lastMid.x + 2.0f * invT * t * lastPt.x + t * t * lastPt.x;
        float by = invT * invT * lastMid.y + 2.0f * invT * t * lastPt.y + t * t * lastPt.y;
        float bp = invT * lastMidPr + t * lastPr;
        m_currentStroke.points.push_back({bx, by, bp});
    }

    // Ensure the last point strictly matches the live cursor coordinate
    m_currentStroke.points.back() = m_rawPoints.back();
}

// ─── appendIncrementalSegment ─────────────────────────────────────────────────
// O(1) incremental append: only the last 3 raw points are touched.
// The rest of m_currentStroke.points is committed and never modified.
//
// Strategy:
//   • n == 1: first real point, nothing to append yet.
//   • n == 2: connect p0→p1 with a mid sample.
//   • n >= 3: compute one new quadratic Bézier arc from mid(p_{n-3},p_{n-2})
//             to mid(p_{n-2},p_{n-1}) through control p_{n-2}, then emit the
//             live tail arc from that midpoint straight to the new cursor.
//
// After appending the real arc we splice in a 1-frame predictive tip:
// the velocity from the last 2 raw points is extrapolated 1 sample forward
// so the visual stroke tip is always ~1 event ahead of the actual position.
// This makes 60Hz feel like sub-frame latency to the human eye.
void PenTool::appendIncrementalSegment() {
    size_t n = m_rawPoints.size();

    // ── Strip old predicted tip before adding real geometry ──────────────
    if (m_hasPredictedTip && !m_currentStroke.points.empty()) {
        m_currentStroke.points.pop_back();
        m_hasPredictedTip = false;
    }
    // Also strip old live-tail (last arc drawn from previous call) so it
    // can be cleanly redrawn with the updated control point.
    if (m_currentStroke.points.size() > m_committedPointCount) {
        m_currentStroke.points.resize(m_committedPointCount);
    }

    if (n == 1) {
        // First point — nothing to connect yet, just ensure it exists
        if (m_currentStroke.points.empty())
            m_currentStroke.points.push_back(m_rawPoints[0]);
        m_committedPointCount = m_currentStroke.points.size();
        return;
    }

    if (n == 2) {
        // Simple line with midpoint for a clean cap
        Vec2f p0{m_rawPoints[0].x, m_rawPoints[0].y};
        Vec2f p1{m_rawPoints[1].x, m_rawPoints[1].y};
        float pr0 = m_rawPoints[0].pressure, pr1 = m_rawPoints[1].pressure;
        // Commit p0 + mid
        if (m_currentStroke.points.empty())
            m_currentStroke.points.push_back({p0.x, p0.y, pr0});
        m_currentStroke.points.push_back({(p0.x+p1.x)*0.5f, (p0.y+p1.y)*0.5f, (pr0+pr1)*0.5f});
        m_committedPointCount = m_currentStroke.points.size();
        // Live tail to cursor
        m_currentStroke.points.push_back({p1.x, p1.y, pr1});
        goto predict_tip;
    }

    {
        // n >= 3: commit one new quadratic Bézier arc (arc from index n-3 to n-2)
        // The arc mid(p_{n-3},p_{n-2}) → p_{n-2} → mid(p_{n-2},p_{n-1})
        // is now fully determined (n-1 is the new live point, but the arc
        // through p_{n-2} only depends on p_{n-3}..p_{n-1}).
        Vec2f prevPt  {m_rawPoints[n-3].x, m_rawPoints[n-3].y};
        Vec2f curPt   {m_rawPoints[n-2].x, m_rawPoints[n-2].y};
        Vec2f nextPt  {m_rawPoints[n-1].x, m_rawPoints[n-1].y};

        Vec2f startMid{(prevPt.x+curPt.x)*0.5f, (prevPt.y+curPt.y)*0.5f};
        Vec2f endMid  {(curPt.x+nextPt.x)*0.5f, (curPt.y+nextPt.y)*0.5f};

        float startPr = (m_rawPoints[n-3].pressure + m_rawPoints[n-2].pressure)*0.5f;
        float curPr   = m_rawPoints[n-2].pressure;
        float endPr   = (m_rawPoints[n-2].pressure + m_rawPoints[n-1].pressure)*0.5f;

        // Emit committed arc (startMid → endMid through curPt) — 4 steps
        constexpr int kSteps = 4;
        for (int s = 1; s <= kSteps; s++) {
            float t    = (float)s / (float)kSteps;
            float invT = 1.0f - t;
            float bx   = invT*invT*startMid.x + 2.0f*invT*t*curPt.x + t*t*endMid.x;
            float by   = invT*invT*startMid.y + 2.0f*invT*t*curPt.y + t*t*endMid.y;
            float bp   = invT*invT*startPr    + 2.0f*invT*t*curPr    + t*t*endPr;
            m_currentStroke.points.push_back({bx, by, bp});
        }
        m_committedPointCount = m_currentStroke.points.size();

        // Live tail arc from endMid straight to the new cursor (p_{n-1})
        Vec2f lastPt{m_rawPoints[n-1].x, m_rawPoints[n-1].y};
        float lastPr = m_rawPoints[n-1].pressure;
        for (int s = 1; s <= 3; s++) {
            float t    = (float)s / 3.0f;
            float invT = 1.0f - t;
            float bx   = invT*invT*endMid.x + 2.0f*invT*t*lastPt.x + t*t*lastPt.x;
            float by   = invT*invT*endMid.y + 2.0f*invT*t*lastPt.y + t*t*lastPt.y;
            float bp   = invT*endPr + t*lastPr;
            m_currentStroke.points.push_back({bx, by, bp});
        }
        // Ensure last point is exactly at cursor
        m_currentStroke.points.back() = m_rawPoints.back();
    }

predict_tip:
    // ── Predictive Tip: 1-frame velocity extrapolation ───────────────────
    // Compute velocity from last 2 raw points and add a ghost point 1 step
    // ahead. Visually eliminates the "stroke lags behind cursor" effect.
    if (n >= 2) {
        const StrokePoint& pA = m_rawPoints[n-2];
        const StrokePoint& pB = m_rawPoints[n-1];
        float vx = pB.x - pA.x;
        float vy = pB.y - pA.y;
        // Clamp prediction to at most 8 canvas units to avoid wild jumps
        // when the user lifts then slams the pen down fast.
        float speed = std::sqrt(vx*vx + vy*vy);
        if (speed > 0.5f && speed < 8.0f) {
            m_currentStroke.points.push_back({
                pB.x + vx, pB.y + vy, pB.pressure
            });
            m_hasPredictedTip = true;
        }
    }
}

void PenTool::onBegan(Vec2f pt, float pressure) {
    m_rawPoints.clear();
    m_currentStroke = Stroke{};
    m_currentStroke.style = m_style;
    m_committedPointCount = 0;
    m_hasPredictedTip     = false;

    m_rawPoints.push_back({pt.x, pt.y, pressure});
    m_currentStroke.points.push_back({pt.x, pt.y, pressure});
    m_committedPointCount = 1;
    m_currentStroke.updateBounds();

    Page* page = m_engine->document().activePage_ptr();
    if (page) {
        uint32_t newId = (uint32_t)(page->strokes.size()) + 1000;
        while (page->strokes.count(newId)) newId++;
        m_currentStroke.id     = newId;
        m_currentStroke.pageId = page->id;
        page->strokes[newId]   = m_currentStroke;
    }
}

void PenTool::onMoved(Vec2f pt, float pressure) {
    if (m_currentStroke.id == 0) return;

    if (!m_rawPoints.empty()) {
        float dx = pt.x - m_rawPoints.back().x;
        float dy = pt.y - m_rawPoints.back().y;
        // Skip micro-movements < 0.3 canvas units (avoids jitter on IR grids)
        if (dx*dx + dy*dy < 0.09f) return;
    }

    m_rawPoints.push_back({pt.x, pt.y, pressure});
    appendIncrementalSegment();  // O(1) — only last 3 points touched
    m_currentStroke.updateBounds();

    Page* page = m_engine->document().activePage_ptr();
    if (page) {
        page->strokes[m_currentStroke.id] = m_currentStroke;
    }
}

void PenTool::onEnded(Vec2f pt) {
    if (m_currentStroke.id == 0) return;

    // Strip predicted tip before final rebuild
    if (m_hasPredictedTip && !m_currentStroke.points.empty()) {
        m_currentStroke.points.pop_back();
        m_hasPredictedTip = false;
    }

    m_rawPoints.push_back({pt.x, pt.y, m_rawPoints.empty() ? 1.0f : m_rawPoints.back().pressure});
    // Full rebuild once on END for a perfectly smooth final stroke
    rebuildStroke();
    m_currentStroke.updateBounds();

    Page* page = m_engine->document().activePage_ptr();
    if (page) {
        page->strokes[m_currentStroke.id] = m_currentStroke;
        m_engine->pushCommand(std::make_unique<AddStrokeCommand>(m_engine, page->id, m_currentStroke));
    }

    m_rawPoints.clear();
    m_currentStroke.id    = 0;
    m_committedPointCount = 0;
    m_hasPredictedTip     = false;
}

void PenTool::onCancelled() {
    if (m_currentStroke.id != 0) {
        Page* page = m_engine->document().activePage_ptr();
        if (page) page->strokes.erase(m_currentStroke.id);
    }
    m_rawPoints.clear();
    m_currentStroke.id    = 0;
    m_committedPointCount = 0;
    m_hasPredictedTip     = false;
}



// ─── Circle-Line Segment Geometric Clipper ────────────────────────────────────

static void clipStrokeWithCircle(const Stroke& origStroke, Vec2f center, float radius, std::vector<Stroke>& outSubStrokes) {
    if (origStroke.points.empty()) return;
    if (origStroke.points.size() == 1) {
        float dx = origStroke.points[0].x - center.x;
        float dy = origStroke.points[0].y - center.y;
        if (dx*dx + dy*dy > radius * radius) {
            outSubStrokes.push_back(origStroke);
        }
        return;
    }

    float r2 = radius * radius;
    const auto& pts = origStroke.points;
    size_t n = pts.size();

    std::vector<std::vector<StrokePoint>> newPolylines;
    std::vector<StrokePoint> currentPoly;

    for (size_t i = 0; i + 1 < n; i++) {
        StrokePoint A = pts[i];
        StrokePoint B = pts[i+1];

        float dx = B.x - A.x;
        float dy = B.y - A.y;
        float segLen2 = dx*dx + dy*dy;

        float fx = A.x - center.x;
        float fy = A.y - center.y;

        float distA2 = fx*fx + fy*fy;
        float distB2 = (B.x - center.x)*(B.x - center.x) + (B.y - center.y)*(B.y - center.y);

        bool aInside = (distA2 <= r2);
        bool bInside = (distB2 <= r2);

        if (segLen2 < 0.0001f) {
            if (!aInside) {
                if (currentPoly.empty()) currentPoly.push_back(A);
            }
            continue;
        }

        // Solve quadratic equation for intersection with circle: |(A - C) + t*(B - A)|^2 = r^2
        float a = segLen2;
        float b = 2.0f * (fx * dx + fy * dy);
        float c = (fx * fx + fy * fy) - r2;

        float discriminant = b * b - 4.0f * a * c;

        float t1 = -1.0f, t2 = -1.0f;
        if (discriminant >= 0.0f) {
            float sq = std::sqrt(discriminant);
            t1 = (-b - sq) / (2.0f * a);
            t2 = (-b + sq) / (2.0f * a);
        }

        float tEnter = -1.0f;
        float tExit  = -1.0f;

        if (t1 >= 0.0f && t1 <= 1.0f) tEnter = t1;
        if (t2 >= 0.0f && t2 <= 1.0f) tExit  = t2;

        if (!aInside && !bInside) {
            if (tEnter >= 0.0f && tExit >= 0.0f && tExit > tEnter) {
                // Pierces through circle:
                if (currentPoly.empty()) currentPoly.push_back(A);
                StrokePoint pEnter = {
                    A.x + tEnter * dx,
                    A.y + tEnter * dy,
                    A.pressure + tEnter * (B.pressure - A.pressure)
                };
                currentPoly.push_back(pEnter);
                newPolylines.push_back(std::move(currentPoly));
                currentPoly.clear();

                StrokePoint pExit = {
                    A.x + tExit * dx,
                    A.y + tExit * dy,
                    A.pressure + tExit * (B.pressure - A.pressure)
                };
                currentPoly.push_back(pExit);
                currentPoly.push_back(B);
            } else {
                if (currentPoly.empty()) currentPoly.push_back(A);
                currentPoly.push_back(B);
            }
        } else if (!aInside && bInside) {
            // Enters circle at tEnter
            if (currentPoly.empty()) currentPoly.push_back(A);
            float t = (tEnter >= 0.0f) ? tEnter : (tExit >= 0.0f ? tExit : 0.5f);
            StrokePoint pEnter = {
                A.x + t * dx,
                A.y + t * dy,
                A.pressure + t * (B.pressure - A.pressure)
            };
            currentPoly.push_back(pEnter);
            newPolylines.push_back(std::move(currentPoly));
            currentPoly.clear();
        } else if (aInside && !bInside) {
            // Exits circle at tExit
            float t = (tExit >= 0.0f) ? tExit : (tEnter >= 0.0f ? tEnter : 0.5f);
            StrokePoint pExit = {
                A.x + t * dx,
                A.y + t * dy,
                A.pressure + t * (B.pressure - A.pressure)
            };
            currentPoly.push_back(pExit);
            currentPoly.push_back(B);
        }
    }

    if (!currentPoly.empty()) {
        newPolylines.push_back(std::move(currentPoly));
    }

    for (auto& poly : newPolylines) {
        if (poly.empty()) continue;
        if (poly.size() == 1) {
            poly.push_back({poly[0].x + 0.05f, poly[0].y + 0.05f, poly[0].pressure});
        }
        Stroke s = origStroke;
        s.points = std::move(poly);
        s.updateBounds();
        outSubStrokes.push_back(std::move(s));
    }
}

// ─── EraserTool ───────────────────────────────────────────────────────────────

EraserTool::EraserTool(WhiteboardEngine* engine)
    : m_engine(engine) {}

void EraserTool::onBegan(Vec2f pt, float /*pressure*/) {
    m_lastPt = pt;
    m_isErasing = true;
    Page* page = m_engine->document().activePage_ptr();
    if (page) {
        m_beforeStrokes = page->strokes;
        m_beforeShapes  = page->shapes;
        m_beforeImages  = page->images;
    }
    if (m_mode == EraserMode::NORMAL && m_engine->renderer()) {
        Vec2f screenPt = m_engine->camera().canvasToScreen(pt);
        float screenR  = m_radius * m_engine->camera().zoom();
        m_engine->renderer()->setLiveEraser(screenPt, screenR);
    }
    eraseAt(pt);
}

void EraserTool::onMoved(Vec2f pt, float /*pressure*/) {
    if (m_mode == EraserMode::NORMAL && m_engine->renderer()) {
        Vec2f screenPt = m_engine->camera().canvasToScreen(pt);
        float screenR  = m_radius * m_engine->camera().zoom();
        m_engine->renderer()->setLiveEraser(screenPt, screenR);
    }
    eraseLine(m_lastPt, pt);
    m_lastPt = pt;
}

void EraserTool::onEnded(Vec2f /*pt*/) {
    if (m_engine->renderer()) {
        m_engine->renderer()->clearLiveEraser();
    }
    if (m_isErasing) {
        m_isErasing = false;
        Page* page = m_engine->document().activePage_ptr();
        if (page) {
            bool changed = (page->strokes.size() != m_beforeStrokes.size()) ||
                           (page->shapes.size() != m_beforeShapes.size()) ||
                           (page->images.size() != m_beforeImages.size());
            if (!changed) {
                for (const auto& [id, s] : page->strokes) {
                    auto it = m_beforeStrokes.find(id);
                    if (it == m_beforeStrokes.end() || it->second.isErased != s.isErased || it->second.points.size() != s.points.size()) {
                        changed = true;
                        break;
                    }
                }
            }
            if (!changed) {
                for (size_t i = 0; i < page->shapes.size(); i++) {
                    if (page->shapes[i].isErased != m_beforeShapes[i].isErased) {
                        changed = true;
                        break;
                    }
                }
            }
            if (changed) {
                m_engine->pushCommand(std::make_unique<EraseCommand>(
                    m_engine, page->id,
                    std::move(m_beforeStrokes), page->strokes,
                    std::move(m_beforeShapes), page->shapes,
                    std::move(m_beforeImages), page->images
                ));
            }
        }
        m_beforeStrokes.clear();
        m_beforeShapes.clear();
        m_beforeImages.clear();
    }
}

void EraserTool::onCancelled() {
    if (m_engine->renderer()) {
        m_engine->renderer()->clearLiveEraser();
    }
    if (m_isErasing) {
        m_isErasing = false;
        Page* page = m_engine->document().activePage_ptr();
        if (page) {
            page->strokes = std::move(m_beforeStrokes);
            page->shapes  = std::move(m_beforeShapes);
            page->images  = std::move(m_beforeImages);
            m_engine->invalidate(DIRTY_ALL);
            if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
        }
        m_beforeStrokes.clear();
        m_beforeShapes.clear();
        m_beforeImages.clear();
    }
}

void EraserTool::eraseLine(Vec2f from, Vec2f to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float dist = std::sqrt(dx*dx + dy*dy);
    float step = std::max(4.0f, m_radius * 0.4f);
    int numSteps = std::max(1, (int)std::ceil(dist / step));

    for (int i = 1; i <= numSteps; i++) {
        float t = (float)i / (float)numSteps;
        Vec2f p = {from.x + t * dx, from.y + t * dy};
        eraseAt(p);
    }
}

void EraserTool::eraseAt(Vec2f pt) {
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    Rectf eraserRect{pt.x - m_radius, pt.y - m_radius,
                     pt.x + m_radius, pt.y + m_radius};
    float r2 = m_radius * m_radius;

    if (m_mode == EraserMode::OBJECT) {
        // Whole Object Eraser: Any touch immediately deletes the entire stroke or shape
        for (auto& [id, stroke] : page->strokes) {
            if (stroke.isErased) continue;
            if (!stroke.bounds.intersects(eraserRect)) continue;
            for (const auto& p : stroke.points) {
                float dx = p.x - pt.x;
                float dy = p.y - pt.y;
                if (dx*dx + dy*dy <= r2) {
                    stroke.isErased = true;
                    break;
                }
            }
        }
        // Also erase shapes whose bounding box is touched by eraser circle
        for (auto& shape : page->shapes) {
            if (shape.isErased) continue;
            float sl = std::min(shape.bounds.left,  shape.bounds.right);
            float sr = std::max(shape.bounds.left,  shape.bounds.right);
            float st = std::min(shape.bounds.top,   shape.bounds.bottom);
            float sb = std::max(shape.bounds.top,   shape.bounds.bottom);
            // Nearest point on rect to eraser center
            float cx = std::max(sl, std::min(sr, pt.x));
            float cy = std::max(st, std::min(sb, pt.y));
            float dx = pt.x - cx, dy = pt.y - cy;
            if (dx*dx + dy*dy <= r2) {
                shape.isErased = true;
            }
        }
    } else {
        // Normal Circular Eraser: Exact geometric circle-line segment clipping for strokes
        // Also erase shapes whose bounds overlap the eraser circle
        std::vector<uint32_t> toDelete;
        std::vector<Stroke> toAdd;

        // Erase shapes touched by eraser circle
        for (auto& shape : page->shapes) {
            if (shape.isErased) continue;
            float sl = std::min(shape.bounds.left,  shape.bounds.right);
            float sr = std::max(shape.bounds.left,  shape.bounds.right);
            float st = std::min(shape.bounds.top,   shape.bounds.bottom);
            float sb = std::max(shape.bounds.top,   shape.bounds.bottom);
            float cx = std::max(sl, std::min(sr, pt.x));
            float cy = std::max(st, std::min(sb, pt.y));
            float dx = pt.x - cx, dy = pt.y - cy;
            if (dx*dx + dy*dy <= r2) {
                shape.isErased = true;
            }
        }

        for (auto& [id, stroke] : page->strokes) {
            if (stroke.isErased || stroke.points.empty()) continue;
            if (!stroke.bounds.intersects(eraserRect)) continue;

            // Check if circle actually touches this stroke
            bool intersects = false;
            for (const auto& p : stroke.points) {
                float dx = p.x - pt.x;
                float dy = p.y - pt.y;
                if (dx*dx + dy*dy <= r2) {
                    intersects = true;
                    break;
                }
            }
            if (!intersects) {
                for (size_t i = 0; i + 1 < stroke.points.size(); i++) {
                    Vec2f a = {stroke.points[i].x, stroke.points[i].y};
                    Vec2f b = {stroke.points[i+1].x, stroke.points[i+1].y};
                    float abx = b.x - a.x, aby = b.y - a.y;
                    float len2 = abx*abx + aby*aby;
                    if (len2 > 0.0001f) {
                        float t = std::clamp(((pt.x - a.x)*abx + (pt.y - a.y)*aby) / len2, 0.0f, 1.0f);
                        float px = a.x + t*abx - pt.x;
                        float py = a.y + t*aby - pt.y;
                        if (px*px + py*py <= r2) {
                            intersects = true;
                            break;
                        }
                    }
                }
            }
            if (!intersects) continue;

            toDelete.push_back(id);
            clipStrokeWithCircle(stroke, pt, m_radius, toAdd);
        }

        for (uint32_t id : toDelete) {
            page->strokes.erase(id);
        }

        for (auto& s : toAdd) {
            uint32_t newId = (uint32_t)(page->strokes.size()) + 2000;
            while (page->strokes.count(newId)) newId++;
            s.id = newId;
            page->strokes[newId] = std::move(s);
        }
    }
}

// ─── SelectionTool ────────────────────────────────────────────────────────────

SelectionTool::SelectionTool(WhiteboardEngine* engine)
    : m_engine(engine) {}

bool SelectionTool::isLocked() const {
    Page* page = m_engine->document().activePage_ptr();
    if (!page || !m_hasSelection) return false;
    for (const auto& [id, stroke] : page->strokes) {
        if (stroke.isSelected && !stroke.isErased) {
            if (stroke.isLocked) return true;
        }
    }
    for (const auto& shape : page->shapes) {
        if (shape.isSelected && !shape.isErased) {
            if (shape.isLocked) return true;
        }
    }
    return false;
}

void SelectionTool::notifySelectionChanged() {
    if (!m_hasSelection) {
        if (m_engine->callbacks().onSelectionChanged) {
            m_engine->callbacks().onSelectionChanged(false, false, 0.f, 0.f, 0.f, 0.f);
        }
        return;
    }
    float cx = (m_selectionBounds.left + m_selectionBounds.right) * 0.5f;
    float cy = (m_selectionBounds.top + m_selectionBounds.bottom) * 0.5f;
    float hw = std::abs(m_selectionBounds.right - m_selectionBounds.left) * 0.5f;
    float hh = std::abs(m_selectionBounds.bottom - m_selectionBounds.top) * 0.5f;
    float cosA = std::cos(m_rotation);
    float sinA = std::sin(m_rotation);

    Vec2f corners[5] = {
        { cx - hw * cosA + hh * sinA, cy - hw * sinA - hh * cosA },
        { cx + hw * cosA + hh * sinA, cy + hw * sinA - hh * cosA },
        { cx + hw * cosA - hh * sinA, cy + hw * sinA + hh * cosA },
        { cx - hw * cosA - hh * sinA, cy - hw * sinA + hh * cosA },
        { cx + (hh + 35.0f) * sinA,   cy - (hh + 35.0f) * cosA }
    };
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int i = 0; i < 5; i++) {
        minX = std::min(minX, corners[i].x);
        minY = std::min(minY, corners[i].y);
        maxX = std::max(maxX, corners[i].x);
        maxY = std::max(maxY, corners[i].y);
    }
    if (m_engine->callbacks().onSelectionChanged) {
        m_engine->callbacks().onSelectionChanged(true, isLocked(), minX, minY, maxX, maxY);
    }
}

void SelectionTool::clearSelection() {
    Page* page = m_engine->document().activePage_ptr();
    if (page) {
        for (auto& [id, stroke] : page->strokes) {
            stroke.isSelected = false;
        }
        for (auto& shape : page->shapes) {
            shape.isSelected = false;
        }
        for (auto& img : page->images) {
            img.isSelected = false;
        }
    }
    m_hasSelection = false;
    m_rotation = 0.0f;
    m_lassoPath.clear();
    notifySelectionChanged();
}

void SelectionTool::selectSingleShape(uint32_t shapeId) {
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    for (auto& [id, stroke] : page->strokes) {
        stroke.isSelected = false;
    }
    for (auto& img : page->images) {
        img.isSelected = false;
    }
    bool found = false;
    for (auto& shape : page->shapes) {
        if (shape.id == shapeId && !shape.isErased) {
            shape.isSelected = true;
            m_selectionBounds = shape.bounds;
            m_rotation = shape.rotation;
            found = true;
        } else {
            shape.isSelected = false;
        }
    }

    m_hasSelection = found;
    m_lassoPath.clear();
    notifySelectionChanged();
}

void SelectionTool::deleteSelected() {
    Page* page = m_engine->document().activePage_ptr();
    if (!page || isLocked() || !m_hasSelection) return;

    std::vector<uint32_t> delStrokes;
    std::vector<uint32_t> delShapes;
    std::vector<uint32_t> delImages;

    for (auto& [id, stroke] : page->strokes) {
        if (stroke.isSelected && !stroke.isLocked && !stroke.isErased) {
            stroke.isErased = true;
            stroke.isSelected = false;
            delStrokes.push_back(id);
        }
    }
    for (auto& shape : page->shapes) {
        if (shape.isSelected && !shape.isLocked && !shape.isErased) {
            shape.isErased = true;
            shape.isSelected = false;
            delShapes.push_back(shape.id);
        }
    }
    for (auto& img : page->images) {
        if (img.isSelected && !img.isLocked && !img.isErased) {
            img.isErased = true;
            img.isSelected = false;
            delImages.push_back(img.id);
        }
    }

    if (!delStrokes.empty() || !delShapes.empty() || !delImages.empty()) {
        m_engine->pushCommand(std::make_unique<DeleteObjectsCommand>(
            m_engine, page->id, std::move(delStrokes), std::move(delShapes), std::move(delImages)
        ));
    }

    m_hasSelection = false;
    m_rotation = 0.0f;
    m_lassoPath.clear();
    notifySelectionChanged();
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void SelectionTool::duplicateSelected() {
    Page* page = m_engine->document().activePage_ptr();
    if (!page || !m_hasSelection || isLocked()) return;

    uint32_t nextStrokeId = 1;
    for (const auto& [id, s] : page->strokes) {
        if (id >= nextStrokeId) nextStrokeId = id + 1;
    }
    uint32_t nextShapeId = (uint32_t)page->shapes.size() + 2000;
    for (const auto& sh : page->shapes) {
        if (sh.id >= nextShapeId) nextShapeId = sh.id + 1;
    }
    uint32_t nextImageId = (uint32_t)page->images.size() + 3000;
    for (const auto& img : page->images) {
        if (img.id >= nextImageId) nextImageId = img.id + 1;
    }

    std::vector<Stroke> newStrokes;
    std::vector<ShapeElement> newShapes;
    std::vector<ImageElement> newImages;
    float offsetX = 40.0f;
    float offsetY = 40.0f;

    for (auto& [id, stroke] : page->strokes) {
        if (stroke.isSelected && !stroke.isErased) {
            stroke.isSelected = false; // Deselect original

            Stroke clone = stroke;
            clone.id = nextStrokeId++;
            clone.isSelected = true; // Select cloned stroke
            clone.isLocked = false;
            for (auto& p : clone.points) {
                p.x += offsetX;
                p.y += offsetY;
            }
            clone.bounds.left   += offsetX;
            clone.bounds.right  += offsetX;
            clone.bounds.top    += offsetY;
            clone.bounds.bottom += offsetY;

            newStrokes.push_back(clone);
        }
    }

    for (auto& shape : page->shapes) {
        if (shape.isSelected && !shape.isErased) {
            shape.isSelected = false; // Deselect original

            ShapeElement clone = shape;
            clone.id = nextShapeId++;
            clone.isSelected = true;
            clone.isLocked = false;
            clone.bounds.left   += offsetX;
            clone.bounds.right  += offsetX;
            clone.bounds.top    += offsetY;
            clone.bounds.bottom += offsetY;

            newShapes.push_back(clone);
        }
    }

    for (auto& img : page->images) {
        if (img.isSelected && !img.isErased) {
            img.isSelected = false;

            ImageElement clone = img;
            clone.id = nextImageId++;
            clone.isSelected = true;
            clone.isLocked = false;
            clone.bounds.left   += offsetX;
            clone.bounds.right  += offsetX;
            clone.bounds.top    += offsetY;
            clone.bounds.bottom += offsetY;

            newImages.push_back(clone);
        }
    }

    for (const auto& s : newStrokes) {
        page->strokes[s.id] = s;
    }
    for (const auto& sh : newShapes) {
        page->shapes.push_back(sh);
    }
    for (const auto& img : newImages) {
        page->images.push_back(img);
    }

    m_selectionBounds.left   += offsetX;
    m_selectionBounds.right  += offsetX;
    m_selectionBounds.top    += offsetY;
    m_selectionBounds.bottom += offsetY;

    if (!newStrokes.empty() || !newShapes.empty() || !newImages.empty()) {
        m_engine->pushCommand(std::make_unique<AddElementsCommand>(
            m_engine, page->id, newStrokes, newShapes, newImages
        ));
    }

    notifySelectionChanged();
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void SelectionTool::lockSelected() {
    Page* page = m_engine->document().activePage_ptr();
    if (!page || !m_hasSelection) return;

    bool currentLock = isLocked();
    bool newLockState = !currentLock;

    for (auto& [id, stroke] : page->strokes) {
        if (stroke.isSelected && !stroke.isErased) {
            stroke.isLocked = newLockState;
        }
    }
    for (auto& shape : page->shapes) {
        if (shape.isSelected && !shape.isErased) {
            shape.isLocked = newLockState;
        }
    }
    for (auto& img : page->images) {
        if (img.isSelected && !img.isErased) {
            img.isLocked = newLockState;
        }
    }

    if (m_engine->callbacks().onSelectionChanged) {
        m_engine->callbacks().onSelectionChanged(true, newLockState, m_selectionBounds.left, m_selectionBounds.top, m_selectionBounds.right, m_selectionBounds.bottom);
    }
}

void SelectionTool::setSelectedColor(Color c) {
    Page* page = m_engine->document().activePage_ptr();
    if (!page || !m_hasSelection || isLocked()) return;

    auto beforeStrokes = page->strokes;
    auto beforeShapes  = page->shapes;
    bool changed = false;

    for (auto& [id, stroke] : page->strokes) {
        if (stroke.isSelected && !stroke.isErased) {
            stroke.style.color = c;
            changed = true;
        }
    }
    for (auto& shape : page->shapes) {
        if (shape.isSelected && !shape.isErased) {
            shape.strokeColor = c;
            changed = true;
        }
    }

    if (changed) {
        m_engine->pushCommand(std::make_unique<ModifyObjectsCommand>(
            m_engine, page->id,
            std::move(beforeStrokes), page->strokes,
            std::move(beforeShapes), page->shapes,
            page->images, page->images
        ));
    }

    m_engine->invalidate(DIRTY_STROKES | DIRTY_SHAPES);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

Color SelectionTool::getSelectedColor() const {
    Page* page = m_engine->document().activePage_ptr();
    if (page && m_hasSelection) {
        for (const auto& [id, stroke] : page->strokes) {
            if (stroke.isSelected && !stroke.isErased) return stroke.style.color;
        }
        for (const auto& shape : page->shapes) {
            if (shape.isSelected && !shape.isErased) return shape.strokeColor;
        }
    }
    return Color{0, 0, 0, 255};
}

void SelectionTool::copySelected() {
    Page* page = m_engine->document().activePage_ptr();
    if (!page || !m_hasSelection) return;

    m_clipboardStrokes.clear();
    m_clipboardShapes.clear();
    m_clipboardImages.clear();
    m_pasteCount = 0;

    for (const auto& [id, stroke] : page->strokes) {
        if (stroke.isSelected && !stroke.isErased) {
            m_clipboardStrokes.push_back(stroke);
        }
    }
    for (const auto& shape : page->shapes) {
        if (shape.isSelected && !shape.isErased) {
            m_clipboardShapes.push_back(shape);
        }
    }
    for (const auto& img : page->images) {
        if (img.isSelected && !img.isErased) {
            m_clipboardImages.push_back(img);
        }
    }
}

bool SelectionTool::hasClipboard() const {
    return !m_clipboardStrokes.empty() || !m_clipboardShapes.empty() || !m_clipboardImages.empty();
}

void SelectionTool::pasteAt(Vec2f targetCenter) {
    if (!hasClipboard()) return;
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    // Calculate bounding box of clipboard content
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& origStroke : m_clipboardStrokes) {
        minX = std::min(minX, origStroke.bounds.left);
        minY = std::min(minY, origStroke.bounds.top);
        maxX = std::max(maxX, origStroke.bounds.right);
        maxY = std::max(maxY, origStroke.bounds.bottom);
    }
    for (const auto& origShape : m_clipboardShapes) {
        float sl = std::min(origShape.bounds.left, origShape.bounds.right);
        float sr = std::max(origShape.bounds.left, origShape.bounds.right);
        float st = std::min(origShape.bounds.top, origShape.bounds.bottom);
        float sb = std::max(origShape.bounds.top, origShape.bounds.bottom);
        minX = std::min(minX, sl);
        minY = std::min(minY, st);
        maxX = std::max(maxX, sr);
        maxY = std::max(maxY, sb);
    }
    for (const auto& origImg : m_clipboardImages) {
        float il = std::min(origImg.bounds.left, origImg.bounds.right);
        float ir = std::max(origImg.bounds.left, origImg.bounds.right);
        float it_ = std::min(origImg.bounds.top, origImg.bounds.bottom);
        float ib = std::max(origImg.bounds.top, origImg.bounds.bottom);
        minX = std::min(minX, il);
        minY = std::min(minY, it_);
        maxX = std::max(maxX, ir);
        maxY = std::max(maxY, ib);
    }

    float clipCenterX = (minX + maxX) * 0.5f;
    float clipCenterY = (minY + maxY) * 0.5f;
    float dx = targetCenter.x - clipCenterX;
    float dy = targetCenter.y - clipCenterY;

    // Deselect previous items on active page
    for (auto& [id, stroke] : page->strokes) {
        stroke.isSelected = false;
    }
    for (auto& shape : page->shapes) {
        shape.isSelected = false;
    }
    for (auto& img : page->images) {
        img.isSelected = false;
    }

    uint32_t nextStrokeId = 1;
    for (const auto& [id, s] : page->strokes) {
        if (id >= nextStrokeId) nextStrokeId = id + 1;
    }
    uint32_t nextShapeId = (uint32_t)page->shapes.size() + 2000;
    uint32_t nextImageId = (uint32_t)page->images.size() + 3000;

    float selMinX = 1e9f, selMinY = 1e9f, selMaxX = -1e9f, selMaxY = -1e9f;

    std::vector<Stroke> addedStrokes;
    std::vector<ShapeElement> addedShapes;
    std::vector<ImageElement> addedImages;

    for (const auto& origStroke : m_clipboardStrokes) {
        Stroke clone = origStroke;
        clone.id = nextStrokeId++;
        clone.isSelected = true;
        clone.isLocked = false;
        for (auto& p : clone.points) {
            p.x += dx;
            p.y += dy;
        }
        clone.bounds.left   += dx;
        clone.bounds.right  += dx;
        clone.bounds.top    += dy;
        clone.bounds.bottom += dy;

        if (clone.bounds.left < selMinX) selMinX = clone.bounds.left;
        if (clone.bounds.top < selMinY) selMinY = clone.bounds.top;
        if (clone.bounds.right > selMaxX) selMaxX = clone.bounds.right;
        if (clone.bounds.bottom > selMaxY) selMaxY = clone.bounds.bottom;

        page->strokes[clone.id] = clone;
        addedStrokes.push_back(clone);
    }

    for (const auto& origShape : m_clipboardShapes) {
        ShapeElement clone = origShape;
        clone.id = nextShapeId++;
        clone.isSelected = true;
        clone.isLocked = false;
        clone.bounds.left   += dx;
        clone.bounds.right  += dx;
        clone.bounds.top    += dy;
        clone.bounds.bottom += dy;

        float sl = std::min(clone.bounds.left, clone.bounds.right);
        float sr = std::max(clone.bounds.left, clone.bounds.right);
        float st = std::min(clone.bounds.top, clone.bounds.bottom);
        float sb = std::max(clone.bounds.top, clone.bounds.bottom);
        if (sl < selMinX) selMinX = sl;
        if (st < selMinY) selMinY = st;
        if (sr > selMaxX) selMaxX = sr;
        if (sb > selMaxY) selMaxY = sb;

        page->shapes.push_back(clone);
        addedShapes.push_back(clone);
    }

    for (const auto& origImg : m_clipboardImages) {
        ImageElement clone = origImg;
        clone.id = nextImageId++;
        clone.isSelected = true;
        clone.isLocked = false;
        clone.bounds.left   += dx;
        clone.bounds.right  += dx;
        clone.bounds.top    += dy;
        clone.bounds.bottom += dy;

        float il = std::min(clone.bounds.left, clone.bounds.right);
        float ir = std::max(clone.bounds.left, clone.bounds.right);
        float it_ = std::min(clone.bounds.top, clone.bounds.bottom);
        float ib = std::max(clone.bounds.top, clone.bounds.bottom);
        if (il < selMinX) selMinX = il;
        if (it_ < selMinY) selMinY = it_;
        if (ir > selMaxX) selMaxX = ir;
        if (ib > selMaxY) selMaxY = ib;

        page->images.push_back(clone);
        addedImages.push_back(clone);
    }

    m_hasSelection = true;
    m_selectionBounds = Rectf{selMinX, selMinY, selMaxX, selMaxY};

    if (!addedStrokes.empty() || !addedShapes.empty() || !addedImages.empty()) {
        m_engine->pushCommand(std::make_unique<AddElementsCommand>(
            m_engine, page->id, std::move(addedStrokes), std::move(addedShapes), std::move(addedImages)
        ));
    }

    if (m_engine->callbacks().onSelectionChanged) {
        m_engine->callbacks().onSelectionChanged(true, false,
            m_selectionBounds.left, m_selectionBounds.top,
            m_selectionBounds.right, m_selectionBounds.bottom);
    }
    m_engine->invalidate(DIRTY_STROKES | DIRTY_SHAPES);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void SelectionTool::paste() {
    if (!hasClipboard()) return;
    m_pasteCount++;
    float offset = 30.0f * (float)m_pasteCount;

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& s : m_clipboardStrokes) {
        minX = std::min(minX, s.bounds.left);
        minY = std::min(minY, s.bounds.top);
        maxX = std::max(maxX, s.bounds.right);
        maxY = std::max(maxY, s.bounds.bottom);
    }
    for (const auto& s : m_clipboardShapes) {
        float sl = std::min(s.bounds.left, s.bounds.right);
        float sr = std::max(s.bounds.left, s.bounds.right);
        float st = std::min(s.bounds.top, s.bounds.bottom);
        float sb = std::max(s.bounds.top, s.bounds.bottom);
        minX = std::min(minX, sl);
        minY = std::min(minY, st);
        maxX = std::max(maxX, sr);
        maxY = std::max(maxY, sb);
    }
    for (const auto& s : m_clipboardImages) {
        float il = std::min(s.bounds.left, s.bounds.right);
        float ir = std::max(s.bounds.left, s.bounds.right);
        float it_ = std::min(s.bounds.top, s.bounds.bottom);
        float ib = std::max(s.bounds.top, s.bounds.bottom);
        minX = std::min(minX, il);
        minY = std::min(minY, it_);
        maxX = std::max(maxX, ir);
        maxY = std::max(maxY, ib);
    }
    float clipCenterX = (minX + maxX) * 0.5f;
    float clipCenterY = (minY + maxY) * 0.5f;
    pasteAt({clipCenterX + offset, clipCenterY + offset});
}

SelectionHandle SelectionTool::hitTestHandle(Vec2f pt) const {
    if (!m_hasSelection) return SelectionHandle::NONE;

    float midX = (m_selectionBounds.left + m_selectionBounds.right) * 0.5f;
    float midY = (m_selectionBounds.top + m_selectionBounds.bottom) * 0.5f;
    float hw = std::abs(m_selectionBounds.right - m_selectionBounds.left) * 0.5f;
    float hh = std::abs(m_selectionBounds.bottom - m_selectionBounds.top) * 0.5f;

    // Transform pt into local unrotated frame
    float cosA = std::cos(-m_rotation);
    float sinA = std::sin(-m_rotation);
    float dx = pt.x - midX;
    float dy = pt.y - midY;
    float lx = dx * cosA - dy * sinA;
    float ly = dx * sinA + dy * cosA;

    // 1. Rotation handle: 35px directly above Top-Center (lx = 0, ly = -hh - 35)
    float rotDx = lx - 0.0f;
    float rotDy = ly - (-hh - 35.0f);
    if (rotDx*rotDx + rotDy*rotDy <= 28.0f * 28.0f) {
        return SelectionHandle::ROTATE;
    }

    // 2. Corner Handles (±hw, ±hh)
    struct LocalHandle { SelectionHandle type; Vec2f pos; };
    LocalHandle cornerHandles[] = {
        { SelectionHandle::TOP_LEFT,     {-hw, -hh} },
        { SelectionHandle::TOP_RIGHT,    { hw, -hh} },
        { SelectionHandle::BOTTOM_RIGHT, { hw,  hh} },
        { SelectionHandle::BOTTOM_LEFT,  {-hw,  hh} }
    };

    float handleHitRadius = 24.0f;
    for (const auto& h : cornerHandles) {
        float hdx = lx - h.pos.x, hdy = ly - h.pos.y;
        if (hdx*hdx + hdy*hdy <= handleHitRadius * handleHitRadius) {
            return h.type;
        }
    }

    // 3. Edge Midpoint Handles
    LocalHandle edgeHandles[] = {
        { SelectionHandle::TOP_CENTER,   {0.0f, -hh} },
        { SelectionHandle::MIDDLE_RIGHT, { hw, 0.0f} },
        { SelectionHandle::BOTTOM_CENTER,{0.0f,  hh} },
        { SelectionHandle::MIDDLE_LEFT,  {-hw, 0.0f} }
    };

    for (const auto& h : edgeHandles) {
        float hdx = lx - h.pos.x, hdy = ly - h.pos.y;
        if (hdx*hdx + hdy*hdy <= handleHitRadius * handleHitRadius) {
            return h.type;
        }
    }

    // 4. Entire Bounding Box Body Hit (with 15px padding margin)
    if (std::abs(lx) <= hw + 15.0f && std::abs(ly) <= hh + 15.0f) {
        return SelectionHandle::BODY;
    }

    return SelectionHandle::NONE;
}

bool SelectionTool::tapSelectAt(Vec2f canvasPt) {
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return false;

    CanvasObject* hitObj = page->findObjectAt(canvasPt, 25.0f);
    if (!hitObj) {
        clearSelection();
        return false;
    }

    page->clearSelection();
    hitObj->setIsSelected(true);
    m_selectionBounds = hitObj->getBounds();
    if (hitObj->getType() == ObjectType::IMAGE) {
        m_rotation = static_cast<ImageElement*>(hitObj)->rotation;
    } else if (hitObj->getType() == ObjectType::SHAPE) {
        m_rotation = static_cast<ShapeElement*>(hitObj)->rotation;
    } else {
        m_rotation = 0.0f;
    }
    m_hasSelection = true;
    notifySelectionChanged();

    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
    return true;
}

void SelectionTool::rotateSelected(float deltaAngle) {
    if (!m_hasSelection || isLocked()) return;
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    Vec2f center = {
        (m_initialSelectionBounds.left + m_initialSelectionBounds.right) * 0.5f,
        (m_initialSelectionBounds.top + m_initialSelectionBounds.bottom) * 0.5f
    };

    m_rotation = std::fmod(m_initialRotation + deltaAngle, 6.28318530718f);

    float cosA = std::cos(deltaAngle);
    float sinA = std::sin(deltaAngle);

    // 1. Rotate Strokes
    for (const auto& [id, origStroke] : m_initialStrokes) {
        auto it = page->strokes.find(id);
        if (it == page->strokes.end()) continue;
        Stroke& s = it->second;
        if (!s.isLocked && !s.isErased) {
            float sMinX = 1e9f, sMinY = 1e9f, sMaxX = -1e9f, sMaxY = -1e9f;
            for (size_t i = 0; i < origStroke.points.size(); i++) {
                float dx = origStroke.points[i].x - center.x;
                float dy = origStroke.points[i].y - center.y;
                s.points[i].x = center.x + dx * cosA - dy * sinA;
                s.points[i].y = center.y + dx * sinA + dy * cosA;

                if (s.points[i].x < sMinX) sMinX = s.points[i].x;
                if (s.points[i].y < sMinY) sMinY = s.points[i].y;
                if (s.points[i].x > sMaxX) sMaxX = s.points[i].x;
                if (s.points[i].y > sMaxY) sMaxY = s.points[i].y;
            }
            float pad = s.style.width * 0.5f;
            s.bounds = {sMinX - pad, sMinY - pad, sMaxX + pad, sMaxY + pad};
        }
    }

    // 2. Rotate Shapes
    for (const auto& [id, origShape] : m_initialShapes) {
        for (auto& sh : page->shapes) {
            if (sh.id == id && !sh.isLocked && !sh.isErased) {
                sh.rotation = std::fmod(origShape.rotation + deltaAngle, 6.28318530718f);

                float origCx = (origShape.bounds.left + origShape.bounds.right) * 0.5f;
                float origCy = (origShape.bounds.top + origShape.bounds.bottom) * 0.5f;
                float hw = (origShape.bounds.right - origShape.bounds.left) * 0.5f;
                float hh = (origShape.bounds.bottom - origShape.bounds.top) * 0.5f;

                float dx = origCx - center.x;
                float dy = origCy - center.y;
                float newCx = center.x + dx * cosA - dy * sinA;
                float newCy = center.y + dx * sinA + dy * cosA;

                sh.bounds.left   = newCx - hw;
                sh.bounds.right  = newCx + hw;
                sh.bounds.top    = newCy - hh;
                sh.bounds.bottom = newCy + hh;
                break;
            }
        }
    }

    // 3. Rotate Images
    for (const auto& [id, origImg] : m_initialImages) {
        for (auto& img : page->images) {
            if (img.id == id && !img.isLocked && !img.isErased) {
                img.rotation = std::fmod(origImg.rotation + deltaAngle, 6.28318530718f);

                float origCx = (origImg.bounds.left + origImg.bounds.right) * 0.5f;
                float origCy = (origImg.bounds.top + origImg.bounds.bottom) * 0.5f;
                float hw = (origImg.bounds.right - origImg.bounds.left) * 0.5f;
                float hh = (origImg.bounds.bottom - origImg.bounds.top) * 0.5f;

                float dx = origCx - center.x;
                float dy = origCy - center.y;
                float newCx = center.x + dx * cosA - dy * sinA;
                float newCy = center.y + dx * sinA + dy * cosA;

                img.bounds.left   = newCx - hw;
                img.bounds.right  = newCx + hw;
                img.bounds.top    = newCy - hh;
                img.bounds.bottom = newCy + hh;
                break;
            }
        }
    }

    m_selectionBounds = m_initialSelectionBounds;
    notifySelectionChanged();
}

void SelectionTool::flipHorizontalSelected() {
    if (!m_hasSelection || isLocked()) return;
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    auto beforeStrokes = page->strokes;
    auto beforeShapes  = page->shapes;
    auto beforeImages  = page->images;

    Rectf selB = m_selectionBounds;
    float midX = (selB.left + selB.right) * 0.5f;

    for (auto& [id, stroke] : page->strokes) {
        if (stroke.isSelected && !stroke.isLocked && !stroke.isErased) {
            for (auto& p : stroke.points) {
                p.x = midX - (p.x - midX);
            }
            stroke.updateBounds();
        }
    }

    for (auto& sh : page->shapes) {
        if (sh.isSelected && !sh.isLocked && !sh.isErased) {
            float origL = sh.bounds.left;
            float origR = sh.bounds.right;
            sh.bounds.left  = midX - (origR - midX);
            sh.bounds.right = midX - (origL - midX);
            sh.flipH = !sh.flipH;
        }
    }

    for (auto& img : page->images) {
        if (img.isSelected && !img.isLocked && !img.isErased) {
            float origL = img.bounds.left;
            float origR = img.bounds.right;
            img.bounds.left  = midX - (origR - midX);
            img.bounds.right = midX - (origL - midX);
            img.flipH = !img.flipH;
        }
    }

    m_engine->pushCommand(std::make_unique<ModifyObjectsCommand>(
        m_engine, page->id,
        std::move(beforeStrokes), page->strokes,
        std::move(beforeShapes), page->shapes,
        std::move(beforeImages), page->images
    ));

    notifySelectionChanged();
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void SelectionTool::flipVerticalSelected() {
    if (!m_hasSelection || isLocked()) return;
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    auto beforeStrokes = page->strokes;
    auto beforeShapes  = page->shapes;
    auto beforeImages  = page->images;

    Rectf selB = m_selectionBounds;
    float midY = (selB.top + selB.bottom) * 0.5f;

    for (auto& [id, stroke] : page->strokes) {
        if (stroke.isSelected && !stroke.isLocked && !stroke.isErased) {
            for (auto& p : stroke.points) {
                p.y = midY - (p.y - midY);
            }
            stroke.updateBounds();
        }
    }

    for (auto& sh : page->shapes) {
        if (sh.isSelected && !sh.isLocked && !sh.isErased) {
            float origT = sh.bounds.top;
            float origB = sh.bounds.bottom;
            sh.bounds.top    = midY - (origB - midY);
            sh.bounds.bottom = midY - (origT - midY);
            sh.flipV = !sh.flipV;
        }
    }

    for (auto& img : page->images) {
        if (img.isSelected && !img.isLocked && !img.isErased) {
            float origT = img.bounds.top;
            float origB = img.bounds.bottom;
            img.bounds.top    = midY - (origB - midY);
            img.bounds.bottom = midY - (origT - midY);
            img.flipV = !img.flipV;
        }
    }

    m_engine->pushCommand(std::make_unique<ModifyObjectsCommand>(
        m_engine, page->id,
        std::move(beforeStrokes), page->strokes,
        std::move(beforeShapes), page->shapes,
        std::move(beforeImages), page->images
    ));

    notifySelectionChanged();
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void SelectionTool::bringToFrontSelected() {
    if (!m_hasSelection || isLocked()) return;
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    auto beforeShapes = page->shapes;
    auto beforeImages = page->images;

    // 1. Images
    if (!page->images.empty()) {
        std::vector<ImageElement> unselected;
        std::vector<ImageElement> selected;
        for (auto& img : page->images) {
            if (img.isSelected && !img.isLocked && !img.isErased) {
                selected.push_back(img);
            } else {
                unselected.push_back(img);
            }
        }
        if (!selected.empty()) {
            unselected.insert(unselected.end(), selected.begin(), selected.end());
            page->images = std::move(unselected);
        }
    }

    // 2. Shapes
    if (!page->shapes.empty()) {
        std::vector<ShapeElement> unselected;
        std::vector<ShapeElement> selected;
        for (auto& sh : page->shapes) {
            if (sh.isSelected && !sh.isLocked && !sh.isErased) {
                selected.push_back(sh);
            } else {
                unselected.push_back(sh);
            }
        }
        if (!selected.empty()) {
            unselected.insert(unselected.end(), selected.begin(), selected.end());
            page->shapes = std::move(unselected);
        }
    }

    m_engine->pushCommand(std::make_unique<ModifyObjectsCommand>(
        m_engine, page->id,
        page->strokes, page->strokes,
        std::move(beforeShapes), page->shapes,
        std::move(beforeImages), page->images
    ));

    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void SelectionTool::sendToBackSelected() {
    if (!m_hasSelection || isLocked()) return;
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    auto beforeShapes = page->shapes;
    auto beforeImages = page->images;

    // 1. Images
    if (!page->images.empty()) {
        std::vector<ImageElement> unselected;
        std::vector<ImageElement> selected;
        for (auto& img : page->images) {
            if (img.isSelected && !img.isLocked && !img.isErased) {
                selected.push_back(img);
            } else {
                unselected.push_back(img);
            }
        }
        if (!selected.empty()) {
            selected.insert(selected.end(), unselected.begin(), unselected.end());
            page->images = std::move(selected);
        }
    }

    // 2. Shapes
    if (!page->shapes.empty()) {
        std::vector<ShapeElement> unselected;
        std::vector<ShapeElement> selected;
        for (auto& sh : page->shapes) {
            if (sh.isSelected && !sh.isLocked && !sh.isErased) {
                selected.push_back(sh);
            } else {
                unselected.push_back(sh);
            }
        }
        if (!selected.empty()) {
            selected.insert(selected.end(), unselected.begin(), unselected.end());
            page->shapes = std::move(selected);
        }
    }

    m_engine->pushCommand(std::make_unique<ModifyObjectsCommand>(
        m_engine, page->id,
        page->strokes, page->strokes,
        std::move(beforeShapes), page->shapes,
        std::move(beforeImages), page->images
    ));

    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void SelectionTool::sendBackwardSelected() {
    if (!m_hasSelection || isLocked()) return;
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    auto beforeShapes = page->shapes;
    auto beforeImages = page->images;

    // 1. Images
    for (size_t i = 1; i < page->images.size(); i++) {
        if (page->images[i].isSelected && !page->images[i-1].isSelected) {
            std::swap(page->images[i], page->images[i-1]);
        }
    }

    // 2. Shapes
    for (size_t i = 1; i < page->shapes.size(); i++) {
        if (page->shapes[i].isSelected && !page->shapes[i-1].isSelected) {
            std::swap(page->shapes[i], page->shapes[i-1]);
        }
    }

    m_engine->pushCommand(std::make_unique<ModifyObjectsCommand>(
        m_engine, page->id,
        page->strokes, page->strokes,
        std::move(beforeShapes), page->shapes,
        std::move(beforeImages), page->images
    ));

    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void SelectionTool::bringForwardSelected() {
    if (!m_hasSelection || isLocked()) return;
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    auto beforeShapes = page->shapes;
    auto beforeImages = page->images;

    // 1. Images
    if (!page->images.empty()) {
        for (int i = (int)page->images.size() - 2; i >= 0; i--) {
            if (page->images[i].isSelected && !page->images[i+1].isSelected) {
                std::swap(page->images[i], page->images[i+1]);
            }
        }
    }

    // 2. Shapes
    if (!page->shapes.empty()) {
        for (int i = (int)page->shapes.size() - 2; i >= 0; i--) {
            if (page->shapes[i].isSelected && !page->shapes[i+1].isSelected) {
                std::swap(page->shapes[i], page->shapes[i+1]);
            }
        }
    }

    m_engine->pushCommand(std::make_unique<ModifyObjectsCommand>(
        m_engine, page->id,
        page->strokes, page->strokes,
        std::move(beforeShapes), page->shapes,
        std::move(beforeImages), page->images
    ));

    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void SelectionTool::scaleSelected(SelectionHandle handle, Vec2f pt) {
    if (!m_hasSelection || isLocked()) return;

    float rawDx = pt.x - m_initialTouchPt.x;
    float rawDy = pt.y - m_initialTouchPt.y;

    float cosA = std::cos(-m_rotation);
    float sinA = std::sin(-m_rotation);
    float dx = rawDx * cosA - rawDy * sinA;
    float dy = rawDx * sinA + rawDy * cosA;

    float origW = m_initialSelectionBounds.right - m_initialSelectionBounds.left;
    float origH = m_initialSelectionBounds.bottom - m_initialSelectionBounds.top;
    if (origW < 0.001f || origH < 0.001f) return;

    float initL = m_initialSelectionBounds.left;
    float initT = m_initialSelectionBounds.top;
    float initR = m_initialSelectionBounds.right;
    float initB = m_initialSelectionBounds.bottom;

    Rectf newB = m_initialSelectionBounds;
    float minSize = 15.0f;
    float denom = origW * origW + origH * origH;
    if (denom < 0.0001f) return;

    switch (handle) {
        // ── 1. Corner Handles: Symmetrical / Aspect-Ratio-Preserving Proportional Scaling ──
        case SelectionHandle::BOTTOM_RIGHT: {
            // Anchor: Top-Left (initL, initT)
            float proj = (dx * origW + dy * origH) / denom;
            float s = std::max(1.0f + proj, std::max(minSize / origW, minSize / origH));
            float newW = origW * s;
            float newH = origH * s;
            newB.left   = initL;
            newB.top    = initT;
            newB.right  = initL + newW;
            newB.bottom = initT + newH;
            break;
        }
        case SelectionHandle::TOP_LEFT: {
            // Anchor: Bottom-Right (initR, initB)
            float proj = ((-dx) * origW + (-dy) * origH) / denom;
            float s = std::max(1.0f + proj, std::max(minSize / origW, minSize / origH));
            float newW = origW * s;
            float newH = origH * s;
            newB.right  = initR;
            newB.bottom = initB;
            newB.left   = initR - newW;
            newB.top    = initB - newH;
            break;
        }
        case SelectionHandle::TOP_RIGHT: {
            // Anchor: Bottom-Left (initL, initB)
            float proj = (dx * origW + (-dy) * origH) / denom;
            float s = std::max(1.0f + proj, std::max(minSize / origW, minSize / origH));
            float newW = origW * s;
            float newH = origH * s;
            newB.left   = initL;
            newB.bottom = initB;
            newB.right  = initL + newW;
            newB.top    = initB - newH;
            break;
        }
        case SelectionHandle::BOTTOM_LEFT: {
            // Anchor: Top-Right (initR, initT)
            float proj = ((-dx) * origW + dy * origH) / denom;
            float s = std::max(1.0f + proj, std::max(minSize / origW, minSize / origH));
            float newW = origW * s;
            float newH = origH * s;
            newB.right  = initR;
            newB.top    = initT;
            newB.left   = initR - newW;
            newB.bottom = initT + newH;
            break;
        }

        // ── 2. Center / Edge Handles: Non-proportional 1D Stretching ──
        case SelectionHandle::TOP_CENTER:
            newB.top = initT + dy;
            if (newB.bottom - newB.top < minSize) newB.top = newB.bottom - minSize;
            newB.left  = initL;
            newB.right = initR;
            break;
        case SelectionHandle::BOTTOM_CENTER:
            newB.bottom = initB + dy;
            if (newB.bottom - newB.top < minSize) newB.bottom = newB.top + minSize;
            newB.left  = initL;
            newB.right = initR;
            break;
        case SelectionHandle::MIDDLE_LEFT:
            newB.left = initL + dx;
            if (newB.right - newB.left < minSize) newB.left = newB.right - minSize;
            newB.top    = initT;
            newB.bottom = initB;
            break;
        case SelectionHandle::MIDDLE_RIGHT:
            newB.right = initR + dx;
            if (newB.right - newB.left < minSize) newB.right = newB.left + minSize;
            newB.top    = initT;
            newB.bottom = initB;
            break;
        default:
            return;
    }

    float scaleX = (newB.right - newB.left) / origW;
    float scaleY = (newB.bottom - newB.top) / origH;

    Page* page = m_engine->document().activePage_ptr();
    if (page) {
        for (const auto& [id, origStroke] : m_initialStrokes) {
            auto it = page->strokes.find(id);
            if (it == page->strokes.end()) continue;
            Stroke& s = it->second;
            if (!s.isLocked && !s.isErased) {
                float sMinX = 1e9f, sMinY = 1e9f, sMaxX = -1e9f, sMaxY = -1e9f;
                for (size_t i = 0; i < origStroke.points.size(); i++) {
                    s.points[i].x = newB.left + (origStroke.points[i].x - m_initialSelectionBounds.left) * scaleX;
                    s.points[i].y = newB.top  + (origStroke.points[i].y - m_initialSelectionBounds.top)  * scaleY;
                    if (s.points[i].x < sMinX) sMinX = s.points[i].x;
                    if (s.points[i].y < sMinY) sMinY = s.points[i].y;
                    if (s.points[i].x > sMaxX) sMaxX = s.points[i].x;
                    if (s.points[i].y > sMaxY) sMaxY = s.points[i].y;
                }
                s.bounds = {sMinX, sMinY, sMaxX, sMaxY};
            }
        }
        for (const auto& [id, origShape] : m_initialShapes) {
            for (auto& sh : page->shapes) {
                if (sh.id == id && !sh.isLocked && !sh.isErased) {
                    sh.bounds.left   = newB.left + (origShape.bounds.left   - m_initialSelectionBounds.left) * scaleX;
                    sh.bounds.right  = newB.left + (origShape.bounds.right  - m_initialSelectionBounds.left) * scaleX;
                    sh.bounds.top    = newB.top  + (origShape.bounds.top    - m_initialSelectionBounds.top)  * scaleY;
                    sh.bounds.bottom = newB.top  + (origShape.bounds.bottom - m_initialSelectionBounds.top)  * scaleY;
                }
            }
        }
        for (const auto& [id, origImg] : m_initialImages) {
            for (auto& img : page->images) {
                if (img.id == id && !img.isLocked && !img.isErased) {
                    img.bounds.left   = newB.left + (origImg.bounds.left   - m_initialSelectionBounds.left) * scaleX;
                    img.bounds.right  = newB.left + (origImg.bounds.right  - m_initialSelectionBounds.left) * scaleX;
                    img.bounds.top    = newB.top  + (origImg.bounds.top    - m_initialSelectionBounds.top)  * scaleY;
                    img.bounds.bottom = newB.top  + (origImg.bounds.bottom - m_initialSelectionBounds.top)  * scaleY;
                }
            }
        }
    }

    m_selectionBounds = newB;
    notifySelectionChanged();
}

void SelectionTool::translateSelected(float dx, float dy) {
    if (!m_hasSelection || isLocked()) return;
    Page* page = m_engine->document().activePage_ptr();
    if (page) {
        for (auto& [id, stroke] : page->strokes) {
            if (stroke.isSelected && !stroke.isErased && !stroke.isLocked) {
                for (auto& p : stroke.points) {
                    p.x += dx;
                    p.y += dy;
                }
                stroke.bounds.left   += dx;
                stroke.bounds.right  += dx;
                stroke.bounds.top    += dy;
                stroke.bounds.bottom += dy;
            }
        }
        for (auto& shape : page->shapes) {
            if (shape.isSelected && !shape.isErased && !shape.isLocked) {
                shape.bounds.left   += dx;
                shape.bounds.right  += dx;
                shape.bounds.top    += dy;
                shape.bounds.bottom += dy;
            }
        }
        for (auto& img : page->images) {
            if (img.isSelected && !img.isErased && !img.isLocked) {
                img.bounds.left   += dx;
                img.bounds.right  += dx;
                img.bounds.top    += dy;
                img.bounds.bottom += dy;
            }
        }
    }
    m_selectionBounds.left   += dx;
    m_selectionBounds.right  += dx;
    m_selectionBounds.top    += dy;
    m_selectionBounds.bottom += dy;

    notifySelectionChanged();
}

void SelectionTool::onBegan(Vec2f pt, float /*p*/) {
    Page* page = m_engine->document().activePage_ptr();
    if (!page) return;

    // 1. If currently selected, check if user touched an active transform handle or inside the selection box
    if (m_hasSelection && !isLocked()) {
        SelectionHandle handle = hitTestHandle(pt);
        if (handle != SelectionHandle::NONE) {
            m_activeHandle = handle;
            m_lastDragPt = pt;
            m_initialTouchPt = pt;
            m_initialSelectionBounds = m_selectionBounds;
            m_initialRotation = m_rotation;
            m_initialStrokes.clear();
            m_initialShapes.clear();
            m_initialImages.clear();
            for (const auto& [id, stroke] : page->strokes) {
                if (stroke.isSelected && !stroke.isErased) {
                    m_initialStrokes[id] = stroke;
                }
            }
            for (const auto& shape : page->shapes) {
                if (shape.isSelected && !shape.isErased) {
                    m_initialShapes[shape.id] = shape;
                }
            }
            for (const auto& img : page->images) {
                if (img.isSelected && !img.isErased) {
                    m_initialImages[img.id] = img;
                }
            }
            return;
        }
    }

    // 2. Direct-touch selection: test if the user touched directly ON an object (image, shape, stroke)
    CanvasObject* hitObj = page->findObjectAt(pt, 25.0f);
    if (hitObj) {
        page->clearSelection();
        hitObj->setIsSelected(true);
        m_selectionBounds = hitObj->getBounds();
        if (hitObj->getType() == ObjectType::IMAGE) {
            m_rotation = static_cast<ImageElement*>(hitObj)->rotation;
        } else if (hitObj->getType() == ObjectType::SHAPE) {
            m_rotation = static_cast<ShapeElement*>(hitObj)->rotation;
        } else {
            m_rotation = 0.0f;
        }
        m_hasSelection = true;
        m_activeHandle = SelectionHandle::BODY;
        m_lastDragPt = pt;
        m_initialTouchPt = pt;
        m_initialSelectionBounds = m_selectionBounds;
        m_initialRotation = m_rotation;
        m_initialStrokes.clear();
        m_initialShapes.clear();
        m_initialImages.clear();
        for (const auto& [id, stroke] : page->strokes) {
            if (stroke.isSelected && !stroke.isErased) {
                m_initialStrokes[id] = stroke;
            }
        }
        for (const auto& shape : page->shapes) {
            if (shape.isSelected && !shape.isErased) {
                m_initialShapes[shape.id] = shape;
            }
        }
        for (const auto& img : page->images) {
            if (img.isSelected && !img.isErased) {
                m_initialImages[img.id] = img;
            }
        }

        notifySelectionChanged();
        m_engine->invalidate(DIRTY_ALL);
        if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
        return;
    }

    // 3. Touch is on empty canvas: clear selection and begin lasso selection marquee
    clearSelection();
    m_activeHandle = SelectionHandle::NONE;
    m_isSelecting = true;
    m_lassoPath.clear();
    m_lassoPath.push_back(pt);
}

void SelectionTool::onMoved(Vec2f pt, float /*p*/) {
    if (m_activeHandle != SelectionHandle::NONE) {
        if (m_activeHandle == SelectionHandle::BODY) {
            float dx = pt.x - m_lastDragPt.x;
            float dy = pt.y - m_lastDragPt.y;
            m_lastDragPt = pt;
            translateSelected(dx, dy);
        } else if (m_activeHandle == SelectionHandle::ROTATE) {
            Vec2f center = {
                (m_initialSelectionBounds.left + m_initialSelectionBounds.right) * 0.5f,
                (m_initialSelectionBounds.top + m_initialSelectionBounds.bottom) * 0.5f
            };
            float startAngle = std::atan2(m_initialTouchPt.y - center.y, m_initialTouchPt.x - center.x);
            float curAngle   = std::atan2(pt.y - center.y, pt.x - center.x);
            rotateSelected(curAngle - startAngle);
            m_lastDragPt = pt;
        } else {
            scaleSelected(m_activeHandle, pt);
            m_lastDragPt = pt;
        }
        return;
    }
    if (!m_isSelecting) return;
    if (m_lassoPath.empty()) {
        m_lassoPath.push_back(pt);
    } else {
        Vec2f last = m_lassoPath.back();
        float dx = pt.x - last.x, dy = pt.y - last.y;
        if (dx*dx + dy*dy > 4.0f) { // 2px minimum point spacing
            m_lassoPath.push_back(pt);
        }
    }
}

void SelectionTool::onEnded(Vec2f pt) {
    if (m_activeHandle != SelectionHandle::NONE) {
        m_activeHandle = SelectionHandle::NONE;
        Page* page = m_engine->document().activePage_ptr();
        if (page) {
            bool changed = false;
            for (const auto& [id, s] : m_initialStrokes) {
                auto it = page->strokes.find(id);
                if (it != page->strokes.end() && (it->second.bounds.left != s.bounds.left || it->second.bounds.top != s.bounds.top)) {
                    changed = true;
                    break;
                }
            }
            if (!changed) {
                for (const auto& sh : page->shapes) {
                    auto it = m_initialShapes.find(sh.id);
                    if (it != m_initialShapes.end() && (sh.bounds.left != it->second.bounds.left || sh.bounds.top != it->second.bounds.top || sh.rotation != it->second.rotation)) {
                        changed = true;
                        break;
                    }
                }
            }
            if (!changed) {
                for (const auto& img : page->images) {
                    auto it = m_initialImages.find(img.id);
                    if (it != m_initialImages.end() && (img.bounds.left != it->second.bounds.left || img.bounds.top != it->second.bounds.top || img.rotation != it->second.rotation)) {
                        changed = true;
                        break;
                    }
                }
            }
            if (changed) {
                m_engine->pushCommand(std::make_unique<ModifyObjectsCommand>(
                    m_engine, page->id,
                    std::move(m_initialStrokes), page->strokes,
                    page->shapes, page->shapes,
                    page->images, page->images
                ));
            }
        }
        m_initialStrokes.clear();
        m_initialShapes.clear();
        m_initialImages.clear();
        return;
    }
    if (!m_isSelecting) return;
    m_isSelecting = false;
    m_lassoPath.push_back(pt);
    if (m_lassoPath.size() > 2) {
        m_lassoPath.push_back(m_lassoPath[0]); // close lasso loop
        performLassoSelection();
    }
    m_lassoPath.clear(); // Clear orange dotted lasso line immediately on release!
    notifySelectionChanged();
}

void SelectionTool::onCancelled() {
    m_isSelecting = false;
    m_lassoPath.clear();
}

void SelectionTool::performLassoSelection() {
    Page* page = m_engine->document().activePage_ptr();
    if (!page || m_lassoPath.size() < 3) return;

    // Compute bounding box of lasso polygon for fast culling
    float lMinX = 1e9f, lMinY = 1e9f, lMaxX = -1e9f, lMaxY = -1e9f;
    for (const auto& pt : m_lassoPath) {
        if (pt.x < lMinX) lMinX = pt.x;
        if (pt.y < lMinY) lMinY = pt.y;
        if (pt.x > lMaxX) lMaxX = pt.x;
        if (pt.y > lMaxY) lMaxY = pt.y;
    }
    Rectf lassoBounds{lMinX, lMinY, lMaxX, lMaxY};

    page->clearSelection();
    page->forEachObject([&](CanvasObject& obj) {
        if (obj.intersectsLasso(m_lassoPath, lassoBounds)) {
            obj.setIsSelected(true);
        }
    });

    m_selectionBounds = page->computeSelectionBounds();
    m_hasSelection = page->hasSelectedObjects();
    m_rotation = 0.0f;
    notifySelectionChanged();
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

// ─── HandTool ─────────────────────────────────────────────────────────────────

HandTool::HandTool(WhiteboardEngine* engine)
    : m_engine(engine) {}

void HandTool::onBegan(Vec2f pt, float /*p*/) {
    m_lastPt = pt;
}

void HandTool::onMoved(Vec2f pt, float /*p*/) {
    float dx = pt.x - m_lastPt.x;
    float dy = pt.y - m_lastPt.y;
    m_lastPt = pt;
    if (m_engine) {
        m_engine->camera().panBy(-dx, -dy);
        m_engine->invalidate(DIRTY_CAMERA);
        if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
    }
}

void HandTool::onEnded(Vec2f /*pt*/) {}
void HandTool::onCancelled() {}

// ─── ShapeTool ────────────────────────────────────────────────────────────────

ShapeTool::ShapeTool(WhiteboardEngine* engine)
    : m_engine(engine) {}

void ShapeTool::onBegan(Vec2f pt, float /*pressure*/) {
    m_startPt = pt;
    m_isDrawing = true;
    m_liveShape.bounds = Rectf{pt.x, pt.y, pt.x, pt.y};
    m_liveShape.shapeType = static_cast<int32_t>(m_shapeType);
    m_liveShape.strokeColor = m_strokeColor;
    m_liveShape.fillColor = m_fillColor;
    m_liveShape.strokeWidth = m_strokeWidth;
}

void ShapeTool::onMoved(Vec2f pt, float /*pressure*/) {
    if (!m_isDrawing) return;
    if (m_shapeType == ShapeType::LINE || m_shapeType == ShapeType::ARROW) {
        m_liveShape.bounds = Rectf{m_startPt.x, m_startPt.y, pt.x, pt.y};
    } else {
        float minX = std::min(m_startPt.x, pt.x);
        float maxX = std::max(m_startPt.x, pt.x);
        float minY = std::min(m_startPt.y, pt.y);
        float maxY = std::max(m_startPt.y, pt.y);
        m_liveShape.bounds = Rectf{minX, minY, maxX, maxY};
    }
}

void ShapeTool::onEnded(Vec2f pt) {
    if (!m_isDrawing) return;
    m_isDrawing = false;

    // If drag was tiny (< 10px), place default sized shape
    float dx = pt.x - m_startPt.x;
    float dy = pt.y - m_startPt.y;
    if (std::sqrt(dx*dx + dy*dy) < 10.0f) {
        if (m_shapeType == ShapeType::LINE || m_shapeType == ShapeType::ARROW) {
            m_liveShape.bounds = Rectf{pt.x - 100.0f, pt.y, pt.x + 100.0f, pt.y};
        } else {
            float defaultSize = 180.0f;
            m_liveShape.bounds = Rectf{pt.x - defaultSize*0.5f, pt.y - defaultSize*0.5f,
                                       pt.x + defaultSize*0.5f, pt.y + defaultSize*0.5f};
        }
    } else {
        if (m_shapeType == ShapeType::LINE || m_shapeType == ShapeType::ARROW) {
            m_liveShape.bounds = Rectf{m_startPt.x, m_startPt.y, pt.x, pt.y};
        } else {
            float minX = std::min(m_startPt.x, pt.x);
            float maxX = std::max(m_startPt.x, pt.x);
            float minY = std::min(m_startPt.y, pt.y);
            float maxY = std::max(m_startPt.y, pt.y);
            if (maxX - minX < 5.0f) maxX = minX + 20.0f;
            if (maxY - minY < 5.0f) maxY = minY + 20.0f;
            m_liveShape.bounds = Rectf{minX, minY, maxX, maxY};
        }
    }

    m_liveShape.shapeType = static_cast<int32_t>(m_shapeType);
    m_liveShape.strokeColor = m_strokeColor;
    m_liveShape.fillColor = m_fillColor;
    m_liveShape.strokeWidth = m_strokeWidth;

    Page* page = m_engine->document().activePage_ptr();
    if (page) {
        m_liveShape.id = (uint32_t)(page->shapes.size()) + 2000;
        m_liveShape.pageId = page->id;
        uint32_t createdId = m_liveShape.id;
        page->shapes.push_back(m_liveShape);
        m_engine->pushCommand(std::make_unique<AddShapeCommand>(m_engine, page->id, m_liveShape));

        // Immediately switch active tool to Selection tool and select this shape!
        m_engine->setActiveTool(ToolType::SELECTION);
        m_engine->tools().selectionTool()->selectSingleShape(createdId);

        m_engine->invalidate(DIRTY_STROKES);
        if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
    }
}

void ShapeTool::onCancelled() {
    m_isDrawing = false;
}

// ─── ToolManager ──────────────────────────────────────────────────────────────

ToolManager::ToolManager(WhiteboardEngine* engine)
    : m_engine(engine)
{
    m_penTool       = std::make_unique<PenTool>(engine);
    m_eraserTool    = std::make_unique<EraserTool>(engine);
    m_selectionTool = std::make_unique<SelectionTool>(engine);
    m_handTool      = std::make_unique<HandTool>(engine);
    m_shapeTool     = std::make_unique<ShapeTool>(engine);
}

void ToolManager::setActiveTool(ActiveTool tool) {
    m_activeTool = tool;
}

static BaseTool* pick(ActiveTool at, PenTool* pen, EraserTool* eraser, SelectionTool* sel, HandTool* hand, ShapeTool* shape) {
    switch (at) {
        case ActiveTool::ERASER:    return eraser;
        case ActiveTool::SELECTION: return sel;
        case ActiveTool::HAND:      return sel->hasSelection() ? static_cast<BaseTool*>(sel) : static_cast<BaseTool*>(hand);
        case ActiveTool::SHAPE:     return shape;
        default:                    return pen;
    }
}

void ToolManager::onTouchBegan(Vec2f pt, float pressure) {
    pick(m_activeTool, m_penTool.get(), m_eraserTool.get(), m_selectionTool.get(), m_handTool.get(), m_shapeTool.get())
        ->onBegan(pt, pressure);
}
void ToolManager::onTouchMoved(Vec2f pt, float pressure) {
    pick(m_activeTool, m_penTool.get(), m_eraserTool.get(), m_selectionTool.get(), m_handTool.get(), m_shapeTool.get())
        ->onMoved(pt, pressure);
}
void ToolManager::onTouchEnded(Vec2f pt) {
    pick(m_activeTool, m_penTool.get(), m_eraserTool.get(), m_selectionTool.get(), m_handTool.get(), m_shapeTool.get())
        ->onEnded(pt);
}
void ToolManager::onTouchCancelled() {
    pick(m_activeTool, m_penTool.get(), m_eraserTool.get(), m_selectionTool.get(), m_handTool.get(), m_shapeTool.get())
        ->onCancelled();
}

} // namespace ob
