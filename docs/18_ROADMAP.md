# 18 — Project Development Roadmap

## Phase 1: Core Engine & Basic Canvas (Months 1–2)
- [x] Architecture design & Documentation suite.
- [ ] Implement C++ `WhiteboardEngine`, `CanvasCamera`, and basic Skia rendering pipeline.
- [ ] Build Pen, Brush, and Eraser tool logic.
- [ ] Implement basic Undo/Redo history stack.
- [ ] Construct Android JNI bridge and SurfaceView container.

## Phase 2: Advanced Tools & File I/O (Months 3–4)
- [ ] Implement Selection, Lasso, Shapes, Text, and Image tools.
- [ ] Build Layer Management System (z-ordering, opacity, visibility).
- [ ] Implement `.obn` native binary file reader/writer with zstd compression.
- [ ] Integrate PDFium for PDF Import and SkPDF for Vector PDF Export.

## Phase 3: Plugin System & Windows Port (Months 5–6)
- [ ] Build C ABI Plugin SDK & `PluginManager` dynamic loader (`dlopen` / `LoadLibrary`).
- [ ] Create QR Code Share Plugin & 3D Shape Tool Plugin reference implementations.
- [ ] Develop Windows WinUI 3 desktop shell and pointer event bridge.
- [ ] Conduct performance profiling, low-latency ink optimization, and security audits.
