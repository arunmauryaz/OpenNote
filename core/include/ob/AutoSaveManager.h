#pragma once

#include "Types.h"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace ob {

class WhiteboardEngine;

// ─── Auto-Save & Crash Recovery Manager ──────────────────────────────────────
//
// File naming:
//   <filesDir>/autosave.obn        – normal periodic save (double-buffered)
//   <filesDir>/crash_recovery.tmp  – written immediately on any dirty change
//   <filesDir>/.clean_exit         – marker written on graceful app close
//
// On startup:
//   if crash_recovery.tmp exists && .clean_exit missing → recovery available

class AutoSaveManager {
public:
    struct RecoveryInfo {
        std::string path;
        int64_t     timestamp    = 0; // Unix ms of last autosave
        int32_t     pageCount    = 0;
    };

    AutoSaveManager(WhiteboardEngine* engine,
                    const std::string& filesDir,
                    int32_t intervalSec);
    ~AutoSaveManager();

    // Start background timer thread
    void start();
    // Stop and join thread (call before destroy)
    void stop();

    // Force an immediate save on the calling thread
    bool saveNow();

    // Call on every document mutation to update crash_recovery.tmp
    void markDirty();

    // Check for crash recovery on app start
    bool hasRecovery() const;
    RecoveryInfo getRecoveryInfo() const;

    // Load crash recovery into engine
    bool performRecovery();

    // Write .clean_exit and remove crash_recovery.tmp
    void writeCleanExit();

    std::function<void(bool success)> onAutoSaveComplete;

private:
    WhiteboardEngine* m_engine;
    std::string       m_filesDir;
    int32_t           m_intervalSec;

    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_dirty{false};
    mutable std::mutex m_mutex;

    std::string autoSavePath()      const;
    std::string recoveryPath()      const;
    std::string cleanExitMarker()   const;

    void backgroundLoop();
    bool writeDocument(const std::string& path);
};

} // namespace ob
