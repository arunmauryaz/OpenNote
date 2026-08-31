#pragma once

// ─── FileFormat — OpenWhiteBoard Binary (.obn) File Format ───────────────────
//
// Byte layout of an .obn file:
//
//   [4]   Magic bytes: "OBN1"
//   [4]   Format version (uint32_t, little-endian) — currently 1
//   [4]   Number of pages (uint32_t)
//   [8]   Creation timestamp (int64_t, Unix ms)
//   [8]   Last modified timestamp (int64_t, Unix ms)
//   [64]  Document title (UTF-8, null-padded)
//
//   Per page (repeated N times):
//     [4]   Page ID (uint32_t)
//     [4]   Page width (float)
//     [4]   Page height (float)
//     [4]   Background color (ARGB uint32_t)
//     [4]   Grid type (uint32_t)
//     [4]   Number of strokes (uint32_t)
//
//     Per stroke:
//       [4]   Stroke ID (uint32_t)
//       [4]   Color ARGB (uint32_t)
//       [4]   Width (float)
//       [4]   Opacity (float)
//       [4]   Number of points (uint32_t)
//
//       Per point (delta-encoded from previous point for compression):
//         [2]   dx (int16_t, fixed-point x16384 — range ±2.0 canvas units)
//         [2]   dy (int16_t, fixed-point x16384)
//         [1]   pressure (uint8_t, 0–255 → 0.0–1.0)
//         (5 bytes per point)
//
// Total overhead per stroke: 20 bytes header + 5 bytes/point
// Typical stroke (50 pts): 270 bytes. 1000 strokes → ~270 KB (vs ~600 KB raw float)
// ─────────────────────────────────────────────────────────────────────────────

#include "ob/Document.h"
#include <string>
#include <cstdint>

namespace ob {

struct ObnSaveResult {
    bool        success = false;
    std::string errorMessage;
    size_t      bytesWritten = 0;
};

struct ObnLoadResult {
    bool        success = false;
    std::string errorMessage;
    size_t      pagesLoaded  = 0;
    size_t      strokesLoaded = 0;
};

// ─── File I/O API ────────────────────────────────────────────────────────────
// Stateless — both functions are thread-safe to call from any thread as long
// as you hold a lock on the Document before passing it in.

ObnSaveResult obnSave(const Document& doc, const std::string& path);
ObnLoadResult obnLoad(Document& doc,       const std::string& path);

// Crash-recovery snapshot (same format, written to a .tmp path)
ObnSaveResult obnSaveSnapshot(const Document& doc, const std::string& snapPath);

// Validate file without loading data
bool obnIsValidFile(const std::string& path);

} // namespace ob
