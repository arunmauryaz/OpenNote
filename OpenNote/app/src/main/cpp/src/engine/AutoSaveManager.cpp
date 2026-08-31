#include "ob/AutoSaveManager.h"
#include "ob/WhiteboardEngine.h"
#include <android/log.h>
#include <fstream>
#include <sys/stat.h>
#include <chrono>

#define LOG_TAG "OB_AutoSave"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ob {

AutoSaveManager::AutoSaveManager(WhiteboardEngine* engine,
                                 const std::string& filesDir,
                                 int32_t intervalSec)
    : m_engine(engine), m_filesDir(filesDir), m_intervalSec(intervalSec) {}

AutoSaveManager::~AutoSaveManager() { stop(); }

std::string AutoSaveManager::autoSavePath()    const { return m_filesDir + "/autosave.obn"; }
std::string AutoSaveManager::recoveryPath()    const { return m_filesDir + "/crash_recovery.tmp"; }
std::string AutoSaveManager::cleanExitMarker() const { return m_filesDir + "/.clean_exit"; }

void AutoSaveManager::start() {
    if (m_running && m_thread.joinable()) {
        LOGI("AutoSaveManager already running");
        return;
    }
    stop();
    m_running = true;
    m_thread  = std::thread([this]{ backgroundLoop(); });
    LOGI("AutoSaveManager started, interval=%ds", m_intervalSec);
}

void AutoSaveManager::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AutoSaveManager::markDirty() {
    m_dirty = true;
}

bool AutoSaveManager::saveNow() {
    if (!m_dirty) return true;
    bool ok = writeDocument(autoSavePath());
    if (ok) {
        m_dirty = false;
        LOGI("Auto-saved to: %s", autoSavePath().c_str());
    } else {
        LOGE("Auto-save FAILED");
    }
    if (onAutoSaveComplete) onAutoSaveComplete(ok);
    return ok;
}

void AutoSaveManager::backgroundLoop() {
    while (m_running) {
        // Sleep in 1s increments so we can check m_running frequently
        for (int i = 0; i < m_intervalSec && m_running; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (m_running && m_dirty) {
            saveNow();
        }
    }
    LOGI("AutoSaveManager background thread exiting");
}

bool AutoSaveManager::writeDocument(const std::string& path) {
    if (!m_engine) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_engine->saveDocument(path);
}

bool AutoSaveManager::hasRecovery() const {
    // Recovery exists if crash_recovery.tmp exists but .clean_exit does not
    std::ifstream recovery(recoveryPath());
    std::ifstream cleanExit(cleanExitMarker());
    return recovery.good() && !cleanExit.good();
}

AutoSaveManager::RecoveryInfo AutoSaveManager::getRecoveryInfo() const {
    RecoveryInfo info;
    info.path = recoveryPath();
    // Read timestamp from file header
    std::ifstream f(recoveryPath(), std::ios::binary);
    if (f.is_open()) {
        char magic[4];
        f.read(magic, 4);
        int64_t ts = 0;
        f.read(reinterpret_cast<char*>(&ts), sizeof(ts));
        info.timestamp  = ts;
        info.pageCount  = 1; // TODO: read from serialized data
    }
    return info;
}

bool AutoSaveManager::performRecovery() {
    LOGI("Performing session recovery from: %s", recoveryPath().c_str());
    // TODO: Deserialize document from crash_recovery.tmp
    // For now just remove the marker so it won't trigger again
    std::remove(cleanExitMarker().c_str());
    return true;
}

void AutoSaveManager::writeCleanExit() {
    std::ofstream marker(cleanExitMarker());
    marker << "ok";
    // Remove crash recovery tmp
    std::remove(recoveryPath().c_str());
    LOGI("Clean exit marker written");
}

} // namespace ob
