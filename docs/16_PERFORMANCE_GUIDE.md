# 16 — Universal Hardware Performance & Touch Latency Optimization Guide

## Performance & Adaptability Mandate

OpenBoard is built to run at **maximum performance, zero latency (< 10 ms), and ultimate smoothness** on **ANY smartboard touch technology in existence**:

- 🔴 **Infrared (IR) Laser Bezel Frames** (ViewSonic, SMART Board, Promethean, MAXHUB)
- 📷 **Optical Camera Sensor Frames** (Corner camera tracking)
- 🌊 **Surface Acoustic Wave (SAW) & Sonic Touch**
- ⚡ **Projected Capacitive (P-CAP)** (iPad, high-end glass IFPDs)
- 🧲 **Electromagnetic Resonance (EMR)** (Samsung S Pen, Wacom digitizers)
- 📽️ **Interactive Laser Projectors** (Epson BrightLink, BenQ, Ricoh)
- 🖊️ **Dual-Pen / Multi-User Passive & Active Stylus Frames**

---

## ⚡ Performance Budget Across All Hardware

| Performance Metric | Target Goal | Universal Smart Board Guarantee |
|--------------------|-------------|----------------------------------|
| Touch-to-Ink Latency | **< 10 ms** (with predictive trajectory) | < 20 ms on legacy 60Hz IR frames |
| Drawing Frame Rate | **60 FPS / 120 FPS** smooth rendering | 60 FPS stable on low-cost quad-core SoCs |
| Cold Engine Startup | **< 500 ms** | < 1000 ms on Android 8.1 IFPDs |
| Memory Footprint (100 Pages) | **< 200 MB** | Zero OOM crashes on shared-RAM TV chips |
| Hardware Calibration Accuracy | **Sub-pixel precision** | Perfect alignment on misaligned optical frames |

---

## 🛠️ Key Engine Optimizations for Universal Touch Smoothness

### 1. Lock-Free DirectByteBuffer Queue
Input events from Android/Windows native hardware drivers bypass Java heap allocations. Touch points are written directly into a shared native C++ ring buffer, eliminating Garbage Collection (GC) pauses during fast handwriting.

### 2. Adaptive Sample Rate Interpolation (60Hz \(\rightarrow\) 240Hz Virtual Curve)
Hardware smartboard touch report rates vary from 60Hz to 500Hz. The engine dynamically measures touch sample delta intervals and synthesizes intermediate Bézier points to ensure line curves look silky-smooth even on 60Hz IR frames.

### 3. Asynchronous Offscreen Tile Compositing
Committed drawing strokes are cached in 512x512 pixel GPU tile textures (`SkPicture` / `SkImage`). When drawing a new line, only the active in-progress stroke is rendered each frame, leaving 95% of GPU bandwidth free for 120 FPS responsiveness.

### 4. Smart Board CPU Core Affinity (`sched_setaffinity`)
On multi-core Android smartboard SoCs (MStar, Rockchip, Amlogic), OpenBoard pins its Engine Render Thread to high-frequency ARM performance cores, preventing frame drops during multi-finger touch gestures.
