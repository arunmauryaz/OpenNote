# 15C — App Settings Panel & Modal Layout Specification

## Overview

Configures application defaults, canvas ratios, performance settings, Hardware Smart Board Digitizer Calibration, and Auto-Save. Designed using a **Minimalist Dark Glassmorphic Dialog Layout** emphasizing smooth 16px corner curves, crisp typography, and clean spatial arrangement.

---

## 🖥️ Settings Modal Layout & Spatial Structure

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ ⚙️ Settings  →  Canvas & App Configuration           [ ← Back to settings ]│
├─────────────────────────────────────────────────────────────────────────────┤
│ Canvas & Page Preset Settings                                               │
│  Default Canvas Mode                       [ Fixed Page (✓) | Infinite ]    │
│  Default Aspect Ratio                      [ 16:9 Widescreen (1920x1080) ▼ ]│
│                                                                             │
│ Auto-Save & Crash Protection                                                │
│  Background Auto-Save Frequency            [ 30 Seconds (Default) ▼        ]│
│  Emergency Recovery Prompt                 (●) Enabled (Capsule Switch)     │
│                                                                             │
│ Hardware Smart Board Calibration                                            │
│  Digitizer Technology Mode                 [ Auto-Detect (✓) | IR Frame ]   │
│  Touch Calibration                         [ 🎯 9-Point Calibration ]       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                            [ Cancel ]   [ Save Settings ]   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 🎨 Background & Grid Picker Specification

```
┌─────────────────────────┬──────────────────────────────────────────┐
│ Background              │ Irlen Filter Colors                      │
│                         │ [🟨 Yellow] [🌸 Pink] [🟩 Green] [🟪 Purple]│
│ [🎨 Originals (1000+)]  │                                          │
│                         │ General Colors                           │
│ [🖼️ Images]              │ [⚪ White] [⬛ Dark] [🟢 Green] [🔵 Blue]  │
│                         │                                          │
│ [🏁 Color & grid   >]   │ Grid Pattern                             │
│                         │ [🚫 None] [▦ Square Grid] [≡ Lines] [∴ Dots]│
│                         │                                          │
│                         │     [ Apply to All Pages (#0A84FF) ]     │
└─────────────────────────┴──────────────────────────────────────────┘
```

---

## 💬 Sudden Shutdown Emergency Recovery Dialog

```
┌──────────────────────────────────────────────────────────────┐
│ ⚠️ Unsaved Session Detected                                   │
├──────────────────────────────────────────────────────────────┤
│ OpenBoard closed unexpectedly (e.g. power failure or sudden   │
│ shutdown).                                                   │
│                                                              │
│ Last auto-saved state: Today at 02:45 PM (12 Pages, 4 Layers) │
│                                                              │
│ Do you want to restore your previous whiteboard session?     │
├──────────────────────────────────────────────────────────────┤
│    [ Discard & Start Fresh ]           [ Restore Session ]   │
└──────────────────────────────────────────────────────────────┘
```
