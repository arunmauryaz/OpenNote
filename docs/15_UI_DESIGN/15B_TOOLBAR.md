# 15B — Main Toolbar & Sub-Menu Flyouts Specification

## Overview

The Main Toolbar dock is centered at the bottom of the screen. Tapping an active tool opens its corresponding Flyout Popup menu.

---

## 🛠️ Main Tool Dock & Flyouts Matrix

| Icon | Tool Name | Sub-Menu Flyout Items (MyViewBoard Spec) | Engine Command |
|------|-----------|------------------------------------------|----------------|
| ↖️ | **Select / Pointer** | Select marquee, multi-select handles, transform controls | `setActiveTool(ToolType::Selection)` |
| ✋ | **Hand Tool (`H`)** | **Canvas Pan Mode**: (Active in Open Canvas Mode; 1-finger pan without drawing ink) | `setActiveTool(ToolType::Pan)` |
| ✏️ | **Pen Tools** | **Pen Flyout**: Standard, Brush, Highlighter, Stamp, Laser, AI Shape Recognizer, Pattern Ink | `setActiveTool(ToolType::Pen)` |
| 🧹 | **Eraser** | **Eraser Flyout**: Standard Pixel, Lasso/Marquee Eraser, Stroke Eraser, Clear Page (Trash), Size Slider | `setActiveTool(ToolType::Eraser)` |
| 📐 | **Shapes & Smart Graphics** | **Shapes Flyout**: Basic 2D (Square, Circle, Triangle), Composite Shapes, Axis Grapher, 3D Cubes, Tables, AI Shape Recognition | `setActiveTool(ToolType::Shape)` |
| **T** | **Text Tools** | **Text Flyout**: Rich Text Box (`T`), Handwriting Recognizer (`Aa`), Voice Speech-to-Text Dictation (`🎙️A`) | `setActiveTool(ToolType::Text)` |
| 📝 | **Sticky Notes** | Color-coded sticky note cards (Yellow, Pink, Green, Blue) | `setActiveTool(ToolType::StickyNote)` |
| 📁 | **Built-In File Manager** | **Custom File Explorer**: Open `.obn`, Import PPTX, Import DOCX, Import PDF, Import Images, Direct USB Drive Navigation, Export Board PDF | `openCustomFileManager()` |
| 🧰 | **Magic Box / Suite** | Widgets, protractor, ruler, 3D shapes, YouTube player | `openMagicBox()` |
| 🌐 | **Web Browser** | Embedded Web Browser overlay for web research & screenshot clipping | `openEmbeddedBrowser()` |
| 📷 | **Screen Capture** | Canvas screenshot, region crop, video recorder | `triggerScreenCapture()` |
| 🎛️ | **Apps & Plugins** | Plugin Manager Grid (installed plugins, custom tools) | `openPluginManagerUI()` |
