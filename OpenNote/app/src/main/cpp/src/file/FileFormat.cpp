#include "ob/FileFormat.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <chrono>
#include <algorithm>

// ─── Endian helpers (always write little-endian) ─────────────────────────────
#ifdef _MSC_VER
#include <intrin.h>
static uint32_t bswap32(uint32_t v) { return _byteswap_ulong(v); }
static uint64_t bswap64(uint64_t v) { return _byteswap_uint64(v); }
#else
static uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }
static uint64_t bswap64(uint64_t v) { return __builtin_bswap64(v); }
#endif

static bool isBigEndian() {
    uint16_t probe = 0x0100;
    return *(uint8_t*)&probe == 0x01;
}

static uint32_t hostToLE32(uint32_t v) { return isBigEndian() ? bswap32(v) : v; }
static uint64_t hostToLE64(uint64_t v) { return isBigEndian() ? bswap64(v) : v; }
static float    hostToLEf(float v)     {
    uint32_t u; std::memcpy(&u, &v, 4);
    u = hostToLE32(u);
    float r; std::memcpy(&r, &u, 4);
    return r;
}

// ─── Helpers: fixed-point encoding for point delta compression ───────────────
static constexpr float DELTA_SCALE                     = 16384.0f;
[[maybe_unused]] static constexpr float DELTA_MAX_CANVAS = 32767.0f / DELTA_SCALE;

namespace ob {

// ─── Write helpers ────────────────────────────────────────────────────────────
static bool writeU8 (FILE* f, uint8_t  v) { return fwrite(&v,1,1,f)==1; }
static bool writeU16(FILE* f, uint16_t v) { v=hostToLE32(v); return fwrite(&v,2,1,f)==1; }
[[maybe_unused]] static bool writeI16(FILE* f, int16_t  v) { return writeU16(f,(uint16_t)v); }
static bool writeU32(FILE* f, uint32_t v) { v=hostToLE32(v); return fwrite(&v,4,1,f)==1; }
static bool writeI64(FILE* f, int64_t  v) { uint64_t u=hostToLE64((uint64_t)v); return fwrite(&u,8,1,f)==1; }
static bool writeF32(FILE* f, float    v) { float le=hostToLEf(v); return fwrite(&le,4,1,f)==1; }
static bool writeStr(FILE* f, const std::string& s, size_t fixedLen) {
    char buf[256]={};
    size_t n = std::min(s.size(), fixedLen-1);
    std::memcpy(buf, s.c_str(), n);
    return fwrite(buf, 1, fixedLen, f) == fixedLen;
}

// ─── Read helpers ─────────────────────────────────────────────────────────────
static bool readU8 (FILE* f, uint8_t&  v) { return fread(&v,1,1,f)==1; }
static bool readU32(FILE* f, uint32_t& v) {
    if (fread(&v,4,1,f)!=1) return false;
    v=hostToLE32(v); return true;
}
static bool readI64(FILE* f, int64_t&  v) {
    uint64_t u; if(fread(&u,8,1,f)!=1) return false;
    u=hostToLE64(u); v=(int64_t)u; return true;
}
static bool readF32(FILE* f, float& v) {
    uint32_t u; if(fread(&u,4,1,f)!=1) return false;
    u=hostToLE32(u); std::memcpy(&v,&u,4); return true;
}
static bool readStr(FILE* f, std::string& s, size_t fixedLen) {
    char buf[256]={};
    if (fread(buf,1,fixedLen,f)!=fixedLen) return false;
    s = std::string(buf, strnlen(buf, fixedLen));
    return true;
}
static bool readI16(FILE* f, int16_t& v) {
    uint16_t u; if(fread(&u,2,1,f)!=1) return false;
    v=(int16_t)hostToLE32((uint32_t)u); return true;
}

// ─── Save ─────────────────────────────────────────────────────────────────────
ObnSaveResult obnSave(const Document& doc, const std::string& path) {
    ObnSaveResult result;

    std::string tmpPath = path + ".tmp";
    FILE* f = fopen(tmpPath.c_str(), "wb");
    if (!f) {
        result.errorMessage = "Cannot open file for write: " + tmpPath;
        return result;
    }

    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // ── File header (Version 4) ──────────────────────────────────────────
    fwrite("OBN1", 1, 4, f);                                // magic
    writeU32(f, 4);                                         // version 4 (lossless float stroke coordinates, slide bg path, shapes, images)
    writeU32(f, (uint32_t)doc.pages.size());                // page count
    writeI64(f, doc.meta.createdAt ? doc.meta.createdAt : nowMs);
    writeI64(f, nowMs);                                     // last modified
    writeStr(f, doc.meta.title, 64);                        // title (64 bytes fixed)

    // ── Pages ─────────────────────────────────────────────────────────────
    for (const auto& page : doc.pages) {
        writeU32(f, page.id);
        writeF32(f, page.width);
        writeF32(f, page.height);

        // Background color as ARGB uint32 (default to white if uninitialized/transparent black)
        Color bgCol = page.background.color;
        if (bgCol.a == 0 && bgCol.r == 0 && bgCol.g == 0 && bgCol.b == 0) {
            bgCol = Color::white();
        }
        uint32_t bgARGB = ((uint32_t)bgCol.a << 24) |
                          ((uint32_t)bgCol.r << 16) |
                          ((uint32_t)bgCol.g << 8)  |
                          ((uint32_t)bgCol.b);
        writeU32(f, bgARGB);
        writeU32(f, (uint32_t)page.background.gridType);
        writeStr(f, page.background.imagePath, 256); // persistent slide background image path

        // Count non-erased strokes
        uint32_t strokeCount = 0;
        for (const auto& [id, s] : page.strokes) {
            if (!s.isErased && !s.points.empty()) strokeCount++;
        }
        writeU32(f, strokeCount);

        // ── Strokes ──────────────────────────────────────────────────────
        for (const auto& [id, s] : page.strokes) {
            if (s.isErased || s.points.empty()) continue;

            uint32_t colorARGB = ((uint32_t)s.style.color.a << 24) |
                                 ((uint32_t)s.style.color.r << 16) |
                                 ((uint32_t)s.style.color.g << 8)  |
                                 ((uint32_t)s.style.color.b);
            writeU32(f, s.id);
            writeU32(f, colorARGB);
            writeF32(f, s.style.width);
            writeF32(f, s.style.opacity);
            writeU8 (f, s.style.penType);
            writeU32(f, (uint32_t)s.points.size());

            // ── Points (lossless 32-bit floats for x, y, pressure, tiltX, tiltY) ──
            for (size_t i = 0; i < s.points.size(); i++) {
                const auto& p = s.points[i];
                writeF32(f, p.x);
                writeF32(f, p.y);
                writeF32(f, p.pressure);
                writeF32(f, p.tiltX);
                writeF32(f, p.tiltY);
            }
        }

        // ── Shapes ───────────────────────────────────────────────────────
        uint32_t shapeCount = 0;
        for (const auto& sh : page.shapes) {
            if (!sh.isErased) shapeCount++;
        }
        writeU32(f, shapeCount);

        for (const auto& sh : page.shapes) {
            if (sh.isErased) continue;
            writeU32(f, sh.id);
            writeU32(f, (uint32_t)sh.shapeType);
            writeF32(f, sh.bounds.left);
            writeF32(f, sh.bounds.top);
            writeF32(f, sh.bounds.right);
            writeF32(f, sh.bounds.bottom);

            uint32_t sColorARGB = ((uint32_t)sh.strokeColor.a << 24) |
                                  ((uint32_t)sh.strokeColor.r << 16) |
                                  ((uint32_t)sh.strokeColor.g << 8)  |
                                  ((uint32_t)sh.strokeColor.b);
            writeU32(f, sColorARGB);

            uint32_t fColorARGB = ((uint32_t)sh.fillColor.a << 24) |
                                  ((uint32_t)sh.fillColor.r << 16) |
                                  ((uint32_t)sh.fillColor.g << 8)  |
                                  ((uint32_t)sh.fillColor.b);
            writeU32(f, fColorARGB);

            writeF32(f, sh.strokeWidth);
            writeU8 (f, sh.isLocked ? 1 : 0);
            writeF32(f, sh.rotation);
            writeU8 (f, sh.flipH ? 1 : 0);
            writeU8 (f, sh.flipV ? 1 : 0);
        }

        // ── Images ───────────────────────────────────────────────────────
        uint32_t imageCount = 0;
        for (const auto& img : page.images) {
            if (!img.isErased) imageCount++;
        }
        writeU32(f, imageCount);

        for (const auto& img : page.images) {
            if (img.isErased) continue;
            writeU32(f, img.id);
            writeF32(f, img.bounds.left);
            writeF32(f, img.bounds.top);
            writeF32(f, img.bounds.right);
            writeF32(f, img.bounds.bottom);
            writeF32(f, img.opacity);
            writeU8 (f, img.isLocked ? 1 : 0);
            writeF32(f, img.rotation);
            writeU8 (f, img.flipH ? 1 : 0);
            writeU8 (f, img.flipV ? 1 : 0);
            writeStr(f, img.imagePath, 256);
        }
    }

    size_t bytesWritten = (size_t)ftell(f);
    fclose(f);

    // Atomic rename
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        result.errorMessage = "Failed to rename tmp file to: " + path;
        return result;
    }

    result.success      = true;
    result.bytesWritten = bytesWritten;
    return result;
}

// ─── Load ─────────────────────────────────────────────────────────────────────
ObnLoadResult obnLoad(Document& doc, const std::string& path) {
    ObnLoadResult result;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        result.errorMessage = "Cannot open file: " + path;
        return result;
    }

    // ── Magic & version ──────────────────────────────────────────────────
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "OBN1", 4) != 0) {
        fclose(f);
        result.errorMessage = "Invalid file format (bad magic)";
        return result;
    }
    uint32_t version = 0;
    readU32(f, version);
    if (version < 1 || version > 4) {
        fclose(f);
        result.errorMessage = "Unsupported .obn version: " + std::to_string(version);
        return result;
    }

    uint32_t pageCount = 0;
    readU32(f, pageCount);
    int64_t createdAt = 0, updatedAt = 0;
    readI64(f, createdAt);
    readI64(f, updatedAt);
    std::string title;
    readStr(f, title, 64);

    doc = Document{};
    doc.meta.createdAt = createdAt;
    doc.meta.updatedAt = updatedAt;
    doc.meta.title     = title;
    doc.pages.clear();
    doc.activePage = 0;

    // ── Pages ─────────────────────────────────────────────────────────────
    for (uint32_t pi = 0; pi < pageCount; pi++) {
        Page page;
        readU32(f, page.id);
        readF32(f, page.width);
        readF32(f, page.height);

        uint32_t bgARGB = 0;
        readU32(f, bgARGB);
        page.background.color.a = (bgARGB >> 24) & 0xFF;
        page.background.color.r = (bgARGB >> 16) & 0xFF;
        page.background.color.g = (bgARGB >> 8)  & 0xFF;
        page.background.color.b = (bgARGB)        & 0xFF;
        if (page.background.color.a == 0 && page.background.color.r == 0 && page.background.color.g == 0 && page.background.color.b == 0) {
            page.background.color = Color::white();
        }
        uint32_t gridType = 0;
        readU32(f, gridType);
        page.background.gridType = (int)gridType;

        if (version >= 3) {
            std::string bgImagePath;
            readStr(f, bgImagePath, 256);
            page.background.imagePath = bgImagePath;
        }

        uint32_t strokeCount = 0;
        readU32(f, strokeCount);

        // ── Strokes ──────────────────────────────────────────────────────
        for (uint32_t si = 0; si < strokeCount; si++) {
            Stroke stroke;
            uint32_t colorARGB = 0;
            readU32(f, stroke.id);
            readU32(f, colorARGB);
            stroke.style.color.a = (colorARGB >> 24) & 0xFF;
            stroke.style.color.r = (colorARGB >> 16) & 0xFF;
            stroke.style.color.g = (colorARGB >> 8)  & 0xFF;
            stroke.style.color.b = (colorARGB)        & 0xFF;
            readF32(f, stroke.style.width);
            readF32(f, stroke.style.opacity);

            if (version >= 2) {
                uint8_t penType = 0;
                readU8(f, penType);
                stroke.style.penType = penType;
            }

            uint32_t ptCount = 0;
            readU32(f, ptCount);
            stroke.points.reserve(ptCount);

            if (version >= 4) {
                for (uint32_t k = 0; k < ptCount; k++) {
                    StrokePoint p;
                    readF32(f, p.x);
                    readF32(f, p.y);
                    readF32(f, p.pressure);
                    readF32(f, p.tiltX);
                    readF32(f, p.tiltY);
                    stroke.points.push_back(p);
                }
            } else {
                float prevX = 0.0f, prevY = 0.0f;
                for (uint32_t k = 0; k < ptCount; k++) {
                    int16_t dx16 = 0, dy16 = 0;
                    uint8_t pressU8 = 255;
                    readI16(f, dx16);
                    readI16(f, dy16);
                    readU8 (f, pressU8);

                    StrokePoint p;
                    p.x        = prevX + (float)dx16 / DELTA_SCALE;
                    p.y        = prevY + (float)dy16 / DELTA_SCALE;
                    p.pressure = (float)pressU8 / 255.0f;
                    stroke.points.push_back(p);
                    prevX = p.x;
                    prevY = p.y;
                }
            }

            stroke.updateBounds();
            page.strokes[stroke.id] = std::move(stroke);
            result.strokesLoaded++;
        }

        // ── Shapes (Version >= 2) ─────────────────────────────────────────
        if (version >= 2) {
            uint32_t shapeCount = 0;
            readU32(f, shapeCount);
            for (uint32_t shi = 0; shi < shapeCount; shi++) {
                ShapeElement sh;
                uint32_t stType = 0, sARGB = 0, fARGB = 0;
                uint8_t lockedU8 = 0, flipH8 = 0, flipV8 = 0;

                readU32(f, sh.id);
                readU32(f, stType);
                sh.shapeType = (int32_t)stType;

                readF32(f, sh.bounds.left);
                readF32(f, sh.bounds.top);
                readF32(f, sh.bounds.right);
                readF32(f, sh.bounds.bottom);

                readU32(f, sARGB);
                sh.strokeColor.a = (sARGB >> 24) & 0xFF;
                sh.strokeColor.r = (sARGB >> 16) & 0xFF;
                sh.strokeColor.g = (sARGB >> 8)  & 0xFF;
                sh.strokeColor.b = (sARGB)        & 0xFF;

                readU32(f, fARGB);
                sh.fillColor.a = (fARGB >> 24) & 0xFF;
                sh.fillColor.r = (fARGB >> 16) & 0xFF;
                sh.fillColor.g = (fARGB >> 8)  & 0xFF;
                sh.fillColor.b = (fARGB)        & 0xFF;

                readF32(f, sh.strokeWidth);
                readU8 (f, lockedU8);
                sh.isLocked = (lockedU8 != 0);

                if (version >= 4) {
                    readF32(f, sh.rotation);
                    readU8 (f, flipH8);
                    readU8 (f, flipV8);
                    sh.flipH = (flipH8 != 0);
                    sh.flipV = (flipV8 != 0);
                }

                page.shapes.push_back(sh);
            }

            uint32_t imageCount = 0;
            readU32(f, imageCount);
            for (uint32_t imgi = 0; imgi < imageCount; imgi++) {
                ImageElement img;
                uint8_t lockedU8 = 0, flipH8 = 0, flipV8 = 0;
                readU32(f, img.id);
                readF32(f, img.bounds.left);
                readF32(f, img.bounds.top);
                readF32(f, img.bounds.right);
                readF32(f, img.bounds.bottom);
                readF32(f, img.opacity);
                readU8 (f, lockedU8);
                img.isLocked = (lockedU8 != 0);

                if (version >= 4) {
                    readF32(f, img.rotation);
                    readU8 (f, flipH8);
                    readU8 (f, flipV8);
                    img.flipH = (flipH8 != 0);
                    img.flipV = (flipV8 != 0);
                }

                readStr(f, img.imagePath, 256);
                page.images.push_back(img);
            }
        }

        doc.pages.push_back(std::move(page));
        result.pagesLoaded++;
    }

    fclose(f);
    result.success = true;
    return result;
}

// ─── Snapshot ─────────────────────────────────────────────────────────────────
ObnSaveResult obnSaveSnapshot(const Document& doc, const std::string& snapPath) {
    FILE* f = fopen(snapPath.c_str(), "wb");
    if (!f) {
        return {false, "Cannot open snapshot file: " + snapPath, 0};
    }
    fclose(f);
    return obnSave(doc, snapPath);
}

// ─── Validate ─────────────────────────────────────────────────────────────────
bool obnIsValidFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[4] = {};
    bool ok = (fread(magic, 1, 4, f) == 4) && (std::memcmp(magic, "OBN1", 4) == 0);
    fclose(f);
    return ok;
}

} // namespace ob