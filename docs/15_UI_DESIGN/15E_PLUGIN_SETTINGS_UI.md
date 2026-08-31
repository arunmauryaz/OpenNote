# 15E — Plugin Settings UI Specification (macOS Minimal Aesthetic)

## Overview

OpenBoard **automatically parses `plugin.json` `settingsSchema`** and renders a high-end macOS/iOS-style dark glassmorphism settings dialog.

---

## 🎨 Auto-Generated Plugin Settings Layout

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 🎛️ Plugin Settings  →  VPS QR Share Plugin             [ ← Back to plugins ]│
├─────────────────────────────────────────────────────────────────────────────┤
│ Server & Credentials                                                        │
│  VPS FTP/HTTP Server URL                   [ https://share.myvps.com      ] │
│  API Key / Password                        [ ••••••••••••••••             ] │
│                                                                             │
│ Preferences                                                                 │
│  Auto-generate QR code on export           (●) Capsule Toggle (ON)          │
│  Link Expiration Period                    [ 24 Hours ▼                   ] │
│                                                                             │
│ Action Buttons                                                              │
│  [ Cancel (Frosted Gray) ]                [ Save Settings (#0A84FF) ]       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## ⚙️ Control Mapping

| Field Type | macOS Design Control Rendered |
|------------|-------------------------------|
| `"string"` / `"url"` | Dark inset text field (`#2C2C2E`) with 1px border (`rgba(255,255,255,0.12)`). |
| `"password"` | Masked text field with lock icon badge. |
| `"boolean"` | macOS-style capsule toggle switch with smooth spring animation. |
| `"choice"` | Segmented pill selector or dark glass dropdown. |
| `"integer"` | Compact numeric stepper with `+` / `-` keycap buttons. |
