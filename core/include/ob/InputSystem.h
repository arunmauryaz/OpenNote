#pragma once

#include "Types.h"
#include <vector>
#include <cstdint>

namespace ob {

// ─── Touch Event (Hardware-Agnostic) ─────────────────────────────────────────

enum class TouchPhase : uint8_t {
    BEGAN    = 0,
    MOVED    = 1,
    ENDED    = 2,
    CANCEL   = 3,
};

struct TouchPointer {
    int32_t  pointerId;      // Unique per touch contact
    float    x, y;           // Screen pixel coordinates
    float    pressure;       // 0.0–1.0 (1.0 for IR grids without pressure)
    float    touchMajor;     // Contact area diameter (palm detection)
    float    touchMinor;
    float    tiltX, tiltY;   // EMR stylus tilt in degrees
    TouchPhase phase;
    int64_t  timestamp;      // Nanoseconds
    bool     isPen = false;
};

struct TouchEvent {
    std::vector<TouchPointer> pointers;
    int32_t                   actionIndex; // Which pointer triggered this event
    bool                      isPen;       // True if stylus/EMR device
};

// ─── Kalman Filter for IR Bezel Jitter ───────────────────────────────────────

struct KalmanState1D {
    float x   = 0.0f;  // State estimate
    float P   = 1.0f;  // Estimate uncertainty
    float Q   = 0.001f; // Process noise
    float R   = 0.1f;  // Measurement noise
};

class KalmanTouchFilter {
public:
    void reset(float x, float y);
    void update(float measX, float measY, float& outX, float& outY);
private:
    KalmanState1D m_kx, m_ky;
};

// ─── Palm Rejection ──────────────────────────────────────────────────────────

class PalmRejector {
public:
    explicit PalmRejector(int32_t radiusThreshold);
    // Returns true if pointer should be treated as palm and rejected
    bool isPalm(const TouchPointer& p) const;
private:
    int32_t m_radiusThreshold;
};

// ─── Input Dispatcher ────────────────────────────────────────────────────────

class ToolManager;
class CanvasCamera;

class InputDispatcher {
public:
    InputDispatcher(ToolManager* tools, CanvasCamera* camera,
                    bool palmRejection, int32_t palmRadiusThresh,
                    bool kalmanEnabled);

    // Primary entry point — called from WhiteboardEngine::onTouchEvent
    void dispatch(const TouchEvent& event);

    // Convert screen → canvas coordinates
    Vec2f screenToCanvas(float sx, float sy) const;

private:
    ToolManager*  m_toolMgr;
    CanvasCamera* m_camera;
    PalmRejector  m_palmRejector;
    KalmanTouchFilter m_kalman;
    bool          m_palmEnabled;
    bool          m_kalmanEnabled;

    // Track two-finger gesture state (pan/zoom)
    struct TwoFingerState {
        bool     active  = false;
        Vec2f    pivot0, pivot1;       // Initial positions
        float    initialSpan = 1.0f;
        float    initialZoom = 1.0f;
        Vec2f    initialPan;
    } m_twoFinger;

    void handleTwoFingerGesture(const TouchEvent& event);
    void handleSingleTouch(const TouchPointer& p, TouchPhase phase);
};

} // namespace ob
