# 06E — Text Tool & Speech Dictation Engine

## Overview

The Text Subsystem provides rich-text layout rendering, real-time handwriting recognition, and live voice dictation. Modeled after MyViewBoard's Text Flyout popup, it supports three input modes.

---

## 🔤 Text Flyout Popup (MyViewBoard Specification)

```
┌────────────────────────────────────────────────────────┐
│  [🔲 Text Box (T)]  [🔤 Handwriting Recognizer (Aa)]  [🎙️ Voice Dictation] │
└────────────────────────────────────────────────────────┘
```

---

## 🛠️ Three Text Input Modes Detail

| Icon | Mode Name | Execution Logic & Workflow |
|------|-----------|----------------------------|
| 🔲 | **Rich Text Box Tool (`T`)** | **Inline OS Text Editor**: Tap anywhere to place a resizable text box. Displays OS keyboard & formatting bar (Fonts, Size, Color, Alignment, Bold, Italic). Renders as Skia `SkTextBlob` when committed. |
| 🔤 | **Handwriting Recognizer (`Aa`)** | **Ink-to-Text Recognizer**: Enables freehand ink writing across the canvas. On `TOUCH_UP`, an offline neural OCR engine parses stroke points, classifies characters/words, and replaces raw ink with formatted text elements. |
| 🎙️ | **Speech-to-Text Dictation (`🎙️A`)** | **Voice Dictation Engine**: Activates device microphone. Transcribes spoken speech into text strings in real-time and inserts text directly at the cursor location on canvas. |
