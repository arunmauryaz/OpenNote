#pragma once

#include "Types.h"
#include <string>

namespace ob {

// ─── Engine Configuration (set once at init) ─────────────────────────────────

struct EngineConfig {
    // Canvas defaults
    float    defaultPageWidth  = 1920.0f;
    float    defaultPageHeight = 1080.0f;

    // Rendering
    int32_t  maxFps            = 60;        // 60 for most boards, 120+ for PCAP
    bool     useVulkan         = false;     // false = OpenGL ES 3.0 (safer compat)
    int32_t  msaaSamples       = 4;
    bool     scissorCulling    = true;
    int32_t  tileSizePx        = 512;

    // Auto-save
    int32_t  autoSaveIntervalSec = 30;
    std::string autoSavePath;              // Set by JNI from Android filesDir

    // Performance
    int32_t  undoHistoryLimit  = 50;       // Max command stack depth
    size_t   strokePoolReserve = 1024;     // Pre-reserved stroke capacity

    // Input
    bool     palmRejection     = true;
    int32_t  touchRadiusThreshold = 350;  // px — contacts > 350px = actual resting palm
    bool     enableKalmanFilter   = false; // Zero-latency real-time touch streaming

    // Plugin
    std::string pluginDir;               // Path scanned for plugin .so files
};

} // namespace ob
