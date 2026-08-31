# 10E — Native PPTX & DOCX Import Engine

## Overview

OpenBoard provides a dedicated **PPTX & DOCX Import Pipeline** that converts Microsoft PowerPoint (`.pptx`) presentations and Word (`.docx`) documents into vector-accurate whiteboard pages. Teachers can import their existing slides as-is, draw annotations over them, and export the complete annotated lesson.

---

## 🏗️ Conversion Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│                   PPTX / DOCX Import Subsystem                           │
└────────────────────────────────────┬─────────────────────────────────────┘
                                     │
                  ┌──────────────────┴──────────────────┐
                  ▼                                     ▼
   ┌─────────────────────────────┐       ┌─────────────────────────────┐
   │ Native XML Parser Pipeline  │       │ Offscreen Headless Converter│
   │ - Unzip PPTX archive        │       │ - LibreOffice / PDFium      │
   │ - Parse ppt/slides/slide*.xml│       │ - Render slides to vector   │
   │ - Extract text, shapes, img │       │   PDF intermediate stream   │
   └──────────────┬──────────────┘       └──────────────┬──────────────┘
                  │                                     │
                  └──────────────────┬──────────────────┘
                                     ▼
   ┌───────────────────────────────────────────────────────────────────────┐
   │ Skia Canvas Page Generator                                            │
   │ - Creates one OpenBoard Page per PPTX slide                           │
   │ - Embeds slide elements onto Background Layer                          │
   │ - Top Layer remains 100% clear for active ink annotations & shapes    │
   └───────────────────────────────────────────────────────────────────────┘
```

---

## 📄 Slide & Page Conversion Specs

1. **Slide Aspect Ratio Preservation**: Detects 16:9 widescreen or 4:3 standard slide layouts and configures canvas page dimensions accordingly.
2. **Layer Separation**:
   - **Background Layer (Locked)**: Houses the rendered PPTX slide background, slide text, original graphics, and embedded images.
   - **Drawing Layers (Active)**: Teachers can freely draw ink, erase, write answers, highlight text, or add 3D shapes without damaging the underlying slide graphics.
3. **Multi-Slide Batch Navigation**: PPTX slides populate the **Pages Manager Panel** automatically, allowing teachers to shuffle slides or switch pages with one tap.
