#ifndef OB_DOCUMENT_H
#define OB_DOCUMENT_H

#include "ob/Types.h"
#include "ob/Elements.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <cstdint>

namespace ob {

struct Background {
    Color color = {255, 255, 255, 255};
    int gridType = 0; // 0: None, 1: Grid, 2: Lines, 3: Dots
    uint32_t textureId = 0; // 0: solid color only, >0: background image texture
    std::string imagePath;  // Local persistent path to slide/background image file
};

struct Page {
    uint32_t id = 1;
    float width  = 1920.0f;
    float height = 1080.0f;
    Background background;
    std::unordered_map<uint32_t, Stroke> strokes;
    std::vector<ShapeElement> shapes;
    std::vector<ImageElement> images;

    // ── Unified Object Management ─────────────────────────────────────────────

    template <typename Func>
    void forEachObject(Func&& callback) {
        for (auto& [id, s] : strokes) {
            if (!s.isErased) callback(static_cast<CanvasObject&>(s));
        }
        for (auto& sh : shapes) {
            if (!sh.isErased) callback(static_cast<CanvasObject&>(sh));
        }
        for (auto& img : images) {
            if (!img.isErased) callback(static_cast<CanvasObject&>(img));
        }
    }

    template <typename Func>
    void forEachObjectConst(Func&& callback) const {
        for (const auto& [id, s] : strokes) {
            if (!s.isErased) callback(static_cast<const CanvasObject&>(s));
        }
        for (const auto& sh : shapes) {
            if (!sh.isErased) callback(static_cast<const CanvasObject&>(sh));
        }
        for (const auto& img : images) {
            if (!img.isErased) callback(static_cast<const CanvasObject&>(img));
        }
    }

    CanvasObject* findObjectAt(Vec2f pt, float hitRadius = 25.0f) {
        for (auto& [id, s] : strokes) {
            if (s.hitTest(pt, hitRadius)) return &s;
        }
        for (auto it = shapes.rbegin(); it != shapes.rend(); ++it) {
            if (it->hitTest(pt, hitRadius)) return &(*it);
        }
        for (auto it = images.rbegin(); it != images.rend(); ++it) {
            if (it->hitTest(pt, hitRadius)) return &(*it);
        }
        return nullptr;
    }

    std::vector<CanvasObject*> getSelectedObjects() {
        std::vector<CanvasObject*> sel;
        for (auto& [id, s] : strokes) {
            if (!s.isErased && s.isSelected) sel.push_back(&s);
        }
        for (auto& sh : shapes) {
            if (!sh.isErased && sh.isSelected) sel.push_back(&sh);
        }
        for (auto& img : images) {
            if (!img.isErased && img.isSelected) sel.push_back(&img);
        }
        return sel;
    }

    bool hasSelectedObjects() const {
        for (const auto& [id, s] : strokes) {
            if (!s.isErased && s.isSelected) return true;
        }
        for (const auto& sh : shapes) {
            if (!sh.isErased && sh.isSelected) return true;
        }
        for (const auto& img : images) {
            if (!img.isErased && img.isSelected) return true;
        }
        return false;
    }

    void clearSelection() {
        for (auto& [id, s] : strokes) s.isSelected = false;
        for (auto& sh : shapes)       sh.isSelected = false;
        for (auto& img : images)      img.isSelected = false;
    }

    void deleteSelected() {
        for (auto& [id, s] : strokes) {
            if (s.isSelected && !s.isLocked) {
                s.isErased = true;
                s.isSelected = false;
            }
        }
        for (auto& sh : shapes) {
            if (sh.isSelected && !sh.isLocked) {
                sh.isErased = true;
                sh.isSelected = false;
            }
        }
        for (auto& img : images) {
            if (img.isSelected && !img.isLocked) {
                img.isErased = true;
                img.isSelected = false;
            }
        }
    }

    void lockSelected(bool lock) {
        for (auto& [id, s] : strokes) {
            if (s.isSelected && !s.isErased) s.isLocked = lock;
        }
        for (auto& sh : shapes) {
            if (sh.isSelected && !sh.isErased) sh.isLocked = lock;
        }
        for (auto& img : images) {
            if (img.isSelected && !img.isErased) img.isLocked = lock;
        }
    }

    Rectf computeSelectionBounds() const {
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        bool found = false;
        for (const auto& [id, s] : strokes) {
            if (!s.isErased && s.isSelected) {
                minX = std::min(minX, s.bounds.left);
                minY = std::min(minY, s.bounds.top);
                maxX = std::max(maxX, s.bounds.right);
                maxY = std::max(maxY, s.bounds.bottom);
                found = true;
            }
        }
        for (const auto& sh : shapes) {
            if (!sh.isErased && sh.isSelected) {
                minX = std::min(minX, std::min(sh.bounds.left, sh.bounds.right));
                minY = std::min(minY, std::min(sh.bounds.top, sh.bounds.bottom));
                maxX = std::max(maxX, std::max(sh.bounds.left, sh.bounds.right));
                maxY = std::max(maxY, std::max(sh.bounds.top, sh.bounds.bottom));
                found = true;
            }
        }
        for (const auto& img : images) {
            if (!img.isErased && img.isSelected) {
                minX = std::min(minX, std::min(img.bounds.left, img.bounds.right));
                minY = std::min(minY, std::min(img.bounds.top, img.bounds.bottom));
                maxX = std::max(maxX, std::max(img.bounds.left, img.bounds.right));
                maxY = std::max(maxY, std::max(img.bounds.top, img.bounds.bottom));
                found = true;
            }
        }
        if (!found) return Rectf{};
        float pad = 8.0f;
        return Rectf{minX - pad, minY - pad, maxX + pad, maxY + pad};
    }
};

struct DocumentMeta {
    std::string title = "Untitled Whiteboard";
    int64_t createdAt = 0;
    int64_t updatedAt = 0;
};

class Document {
public:
    Document() { appendNewPage(); }

    DocumentMeta meta;
    std::vector<Page> pages;
    uint32_t activePage = 0;

    Page* activePage_ptr() {
        if (activePage < pages.size()) return &pages[activePage];
        return pages.empty() ? nullptr : &pages[0];
    }
    const Page* activePage_ptr() const {
        if (activePage < pages.size()) return &pages[activePage];
        return pages.empty() ? nullptr : &pages[0];
    }

    void appendNewPage() {
        Page p;
        uint32_t maxId = 0;
        for (const auto& pg : pages) {
            if (pg.id > maxId) maxId = pg.id;
        }
        p.id = maxId + 1;
        pages.push_back(p);
    }

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
};

} // namespace ob

#endif // OB_DOCUMENT_H
