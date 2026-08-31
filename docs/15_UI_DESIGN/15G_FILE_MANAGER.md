# 15G — Custom Built-In File Manager Specification

## Overview

Rather than forcing users to rely solely on Android's generic system picker, OpenBoard includes its own **Built-In Custom File Manager & Media Explorer**. Optimized for Smart Board touch displays and Android TV, it provides visual thumbnail previews, color-coded file icons, direct USB drive browsing, and seamless document export.

---

## 🎨 Built-In File Manager UI Layout

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 📁 OpenBoard File Manager & Media Explorer                                X │
├──────────────────────────┬──────────────────────────────────────────────────┤
│ Fast Storage Nav:        │ Recent Teaching Sessions & Downloads             │
│ 📄 Recent Sessions       │ ┌──────────────┐ ┌──────────────┐ ┌───────────┐ │
│ 💾 Internal Storage      │ │ 📄 Math_L01  │ │ 📊 Phys_Ch2  │ │ 📄 Bio_Q4 │ │
│ 🔌 USB Drive (OTG)       │ │ .obn (12 pgs)│ │ .pptx (25 p) │ │ .pdf      │ │
│ 📁 Downloads             │ └──────────────┘ └──────────────┘ └───────────┘ │
│                          │                                                  │
│ Filter by Type:          │ File Browser Grid:                               │
│ [All] [.obn] [PDF] [PPTX]│ 📄 Lecture_Algebra.obn     (300 DPI, 8 MB)        │
│ [DOCX] [Images]          │ 📊 Physics_Mechanics.pptx  (24 Slides)          │
│                          │ 📄 Chemistry_Lab.pdf       (18 Pages)           │
├──────────────────────────┴──────────────────────────────────────────────────┤
│ 💡 Selected: Lecture_Algebra.obn   │ [ 📤 Export Annotated PDF ] [ 📂 Open ] │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## ⚙️ Key File Manager Capabilities

1. **Color-Coded File Badges & Icons**:
   - 📄 **.obn (OpenBoard Native)**: Gold badge with page count indicator.
   - 📊 **.pptx / .ppt**: Orange presentation icon.
   - 📄 **.pdf**: Red PDF badge.
   - 📝 **.docx**: Blue document badge.
   - 🖼️ **Images (.png, .jpg, .webp)**: Live image thumbnail preview.
2. **Direct USB & External Storage Access**:
   - Automatically detects OTG USB Flash Drives plugged into Android Smart Boards or TVs.
   - Allows teachers to insert a USB drive, open a PPTX directly, teach, and export the annotated PDF back to the USB drive.
3. **Thumbnail Generation Engine**:
   - Asynchronously extracts embedded slide previews for PPTX/PDF and renders 128x128 thumbnail snapshots for `.obn` boards.
4. **Fallback System Picker**:
   - Includes a **"Browse System Cloud Storage..."** button to fallback to Android Storage Access Framework (SAF) when accessing Google Drive or OneDrive.
