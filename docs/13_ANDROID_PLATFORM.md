# 13 — Android Smart Board & IFPD Hardware Layer

## Overview

Commercial Android Smart Boards (Interactive Flat Panel Displays) operate on specialized SoC chipsets (MStar/MediaTek, Rockchip RK3588, Amlogic, Realtek) running customized vendor Android builds (Android 8.1 up to Android 13). 

OpenBoard is architected to deliver flawless 60 FPS / 120 FPS performance across **all Android Smart Board chipsets**, bypassing vendor-specific bottlenecks through low-level C++ rendering and hardware touch adaptation.

---

## 🖥️ Smart Board Chipset Compatibility Matrix

| Board Chipset Family | Target Smart Board Brands | Hardware Adaptations & Optimizations |
|----------------------|---------------------------|--------------------------------------|
| **MStar / MediaTek** (e.g. MS6A848, MT9950) | ViewSonic, MAXHUB,newline | Use OpenGL ES 3.0 fallback path; optimize texture memory buffers for shared RAM architecture. |
| **Rockchip** (e.g. RK3588, RK3399) | Custom Education Smartboards | Full Vulkan 1.2 hardware acceleration; multi-core render thread affinity (`CPU 4-7`). |
| **Amlogic** (e.g. T972, T982) | Interactive TV / Projectors | Reduced EGL swap interval latency; auto-downsample offscreen surfaces to 4K native framebuffer. |
| **Qualcomm / Samsung** | High-End IFPDs & Tablets | Vulkan GPU pipeline + S Pen / Active EMR Stylus low-latency direct prediction. |

---

## 📺 Smart Board Screen Size & Resolution Adaptability

Smart Boards range from 55-inch classroom panels to 105-inch ultra-wide 21:9 displays at 4K UHD (3840x2160) or 8K.

```cpp
class SmartBoardScreenAdapter {
public:
    static float calculateUIFontScale(int screenWidth, int screenHeight, float densityDpi) {
        // Normalize UI element dimensions so toolbars and pen flyouts 
        // remain ergonomically sized on an 86-inch 4K screen as well as a 55-inch 1080p panel
        float diagonalInches = std::sqrt(screenWidth * screenWidth + screenHeight * screenHeight) / densityDpi;
        if (diagonalInches > 70.0f) {
            return 1.25f; // Scale UI up for large classroom viewing distance
        }
        return 1.0f;
    }
};
```
