#include "ob/InputSystem.h"
#include "ob/CanvasCamera.h"
#include "ob/Tools.h"
#include <cmath>
#include <android/log.h>

#define LOG_TAG "OB_Input"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

namespace ob {

// ─── KalmanTouchFilter ────────────────────────────────────────────────────────

void KalmanTouchFilter::reset(float x, float y) {
    // Q (process noise) = 0.001: trust that position doesn't jump wildly between samples.
    // R (measurement noise) = 0.02: IR smartboard sensors are accurate; trust the raw
    // reading heavily so the filter introduces < 2% lag per step instead of the old 10%.
    // Lower R = faster tracking = less perceived latency. For noisy capacitive grids raise R.
    m_kx = KalmanState1D{x, 1.0f, 0.001f, 0.02f};
    m_ky = KalmanState1D{y, 1.0f, 0.001f, 0.02f};
}

static float kalmanStep(KalmanState1D& k, float measurement) {
    // Predict
    k.P += k.Q;
    // Update (Kalman gain)
    float gain = k.P / (k.P + k.R);
    k.x = k.x + gain * (measurement - k.x);
    k.P = (1.0f - gain) * k.P;
    return k.x;
}

void KalmanTouchFilter::update(float measX, float measY, float& outX, float& outY) {
    outX = kalmanStep(m_kx, measX);
    outY = kalmanStep(m_ky, measY);
}

// ─── PalmRejector ─────────────────────────────────────────────────────────────

PalmRejector::PalmRejector(int32_t radiusThreshold)
    : m_radiusThreshold(radiusThreshold) {}

bool PalmRejector::isPalm(const TouchPointer& p) const {
    // Stylus / EMR pen input is NEVER rejected as palm
    if (p.isPen) return false;
    // Reject only if touch contact diameter exceeds configured threshold (or fallback default 220px)
    float thresh = m_radiusThreshold > 0 ? static_cast<float>(m_radiusThreshold) : 220.0f;
    return p.touchMajor > thresh;
}

// ─── InputDispatcher ──────────────────────────────────────────────────────────

InputDispatcher::InputDispatcher(ToolManager* tools, CanvasCamera* camera,
                                 bool palmRejection, int32_t palmRadiusThresh,
                                 bool kalmanEnabled)
    : m_toolMgr(tools)
    , m_camera(camera)
    , m_palmRejector(palmRadiusThresh)
    , m_palmEnabled(palmRejection)
    , m_kalmanEnabled(kalmanEnabled)
{}

void InputDispatcher::dispatch(const TouchEvent& event) {
    if (event.pointers.empty()) return;

    // ── Two-finger gesture (pinch/pan) — ONLY active when Hand Tool is selected ──
    if (event.pointers.size() >= 2 && m_toolMgr->activeTool() == ActiveTool::HAND) {
        handleTwoFingerGesture(event);
        return;
    }

    // Single pointer
    const TouchPointer& p = event.pointers[event.actionIndex];

    // Palm rejection
    if (m_palmEnabled && m_palmRejector.isPalm(p)) {
        LOGI("Palm rejected: touchMajor=%.1f", p.touchMajor);
        return;
    }

    handleSingleTouch(p, p.phase);
}

void InputDispatcher::handleSingleTouch(const TouchPointer& p, TouchPhase phase) {
    float sx = p.x, sy = p.y;

    // Kalman filtering for jitter reduction (IR bezel / budget sensors)
    if (m_kalmanEnabled) {
        if (phase == TouchPhase::BEGAN) {
            m_kalman.reset(sx, sy);
        } else {
            m_kalman.update(sx, sy, sx, sy);
        }
    }

    // Screen → Canvas coordinate transform
    Vec2f canvasPt = m_camera->screenToCanvas({sx, sy});
    float pressure  = (p.pressure > 0.01f) ? p.pressure : 1.0f;

    switch (phase) {
        case TouchPhase::BEGAN:
            // End any active two-finger gesture first
            m_twoFinger.active = false;
            m_toolMgr->onTouchBegan(canvasPt, pressure);
            break;
        case TouchPhase::MOVED:
            m_toolMgr->onTouchMoved(canvasPt, pressure);
            break;
        case TouchPhase::ENDED:
            m_toolMgr->onTouchEnded(canvasPt);
            break;
        case TouchPhase::CANCEL:
            m_toolMgr->onTouchCancelled();
            break;
    }
}

void InputDispatcher::handleTwoFingerGesture(const TouchEvent& event) {
    if (event.pointers.size() < 2) return;

    const TouchPointer& p0 = event.pointers[0];
    const TouchPointer& p1 = event.pointers[1];

    Vec2f s0{p0.x, p0.y};
    Vec2f s1{p1.x, p1.y};
    float span = std::sqrt((s1.x-s0.x)*(s1.x-s0.x) + (s1.y-s0.y)*(s1.y-s0.y));
    Vec2f mid{(s0.x+s1.x)*0.5f, (s0.y+s1.y)*0.5f};

    // Cancel any active single-touch drawing
    if (!m_twoFinger.active) {
        m_toolMgr->onTouchCancelled();
    }

    // Determine action from first active pointer's phase
    TouchPhase phase = p0.phase;

    if (!m_twoFinger.active) {
        if (phase == TouchPhase::BEGAN || phase == TouchPhase::MOVED) {
            // Initialize gesture
            m_twoFinger.active      = true;
            m_twoFinger.pivot0      = s0;
            m_twoFinger.pivot1      = s1;
            m_twoFinger.initialSpan = std::max(span, 1.0f);
            m_twoFinger.initialZoom = m_camera->zoom();
            m_twoFinger.initialPan  = m_camera->pan();
        }
        return;
    }

    if (phase == TouchPhase::ENDED || phase == TouchPhase::CANCEL) {
        m_twoFinger.active = false;
        return;
    }

    // Pinch-zoom: ratio of current span to initial span
    float zoomFactor = span / m_twoFinger.initialSpan;
    float targetZoom = m_twoFinger.initialZoom * zoomFactor;
    m_camera->zoomToLevel(targetZoom, mid);

    // Pan: midpoint delta since gesture started
    Vec2f initialMid{(m_twoFinger.pivot0.x + m_twoFinger.pivot1.x)*0.5f,
                     (m_twoFinger.pivot0.y + m_twoFinger.pivot1.y)*0.5f};
    float panDx = mid.x - initialMid.x;
    float panDy = mid.y - initialMid.y;

    // Reset pivot so next frame delta is incremental
    m_twoFinger.pivot0 = s0;
    m_twoFinger.pivot1 = s1;
    m_twoFinger.initialSpan = span;
    m_twoFinger.initialZoom = m_camera->zoom();

    m_camera->panBy(panDx, panDy);
}

Vec2f InputDispatcher::screenToCanvas(float sx, float sy) const {
    return m_camera->screenToCanvas({sx, sy});
}

} // namespace ob
