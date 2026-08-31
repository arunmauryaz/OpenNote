# 10B — Vector PDF Export & Student Notes Sharing Engine

## Overview

OpenBoard features a dedicated **Annotated Lesson Notes PDF Export Engine**. It merges imported PPTX/PDF slide backgrounds with all ink markings, written answers, shapes, sticky notes, and annotations created by the teacher during class into a clean vector PDF note for student distribution.

---

## 📤 Annotated Board PDF Export Pipeline

```
┌──────────────────────────────────────────────────────────────────────────┐
│                 Annotated PDF Export Trigger (File Manager)               │
└────────────────────────────────────┬─────────────────────────────────────┘
                                     │
                                     ▼
   ┌───────────────────────────────────────────────────────────────────────┐
   │ Skia SkPDF Multi-Page Renderer                                        │
   │                                                                       │
   │ For each Page (1 .. N):                                               │
   │   1. Render Slide Background (Original PPTX / PDF background graphics) │
   │   2. Render Vector Ink Strokes (Teacher handwriting & answers)        │
   │   3. Render Text Elements (Typed notes & dictation)                  │
   │   4. Render Shapes & Diagrams (3D cubes, coordinate graphs, tables)   │
   └─────────────────────────────────┬─────────────────────────────────────┘
                                     │
                                     ▼
   ┌───────────────────────────────────────────────────────────────────────┐
   │ Output Destination via File Manager                                   │
   │ - Save directly to USB Flash Drive (`/storage/usbotg/...`)             │
   │ - Save to Internal Storage Downloads (`/sdcard/Download/...`)          │
   │ - One-Tap QR Code Share (Upload & generate QR code for students)      │
   └───────────────────────────────────────────────────────────────────────┘
```

---

## 🎯 Key Export Options

1. **Resolution & Vector Preservation**:
   - Ink strokes, shapes, and typed text are exported as native PDF vector objects (`SkTextBlob` & `SkPath`) for crisp printing and zooming.
2. **Export Selection**:
   - Export Entire Document (All Slides/Pages).
   - Export Current Page Only.
   - Export Custom Page Range (e.g. Slides 1–15).
3. **Student Distribution Channels**:
   - **File Manager Save**: Save `.pdf` to local folder or USB drive.
   - **QR Code Share**: Upload PDF to VPS server via plugin and display QR code on board for students to scan with smartphones.
