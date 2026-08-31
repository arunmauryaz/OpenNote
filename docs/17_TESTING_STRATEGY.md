# 17 — Testing Strategy & Quality Assurance

## Testing Architecture

```
                       ┌─────────────────────────┐
                       │  E2E / UI Tests (10%)   │
                       │  (Espresso / WinAppDriver)│
                       └────────────┬────────────┘
                                    │
                       ┌────────────┴────────────┐
                       │ Integration Tests (30%) │
                       │ (C++ API + File Roundtrip)│
                       └────────────┬────────────┘
                                    │
                       ┌────────────┴────────────┐
                       │    Unit Tests (60%)     │
                       │   (Catch2 / GoogleTest) │
                       └─────────────────────────┘
```

---

## Testing Frameworks

1. **C++ Unit Tests**: Built with **Catch2**. Covers vector calculations, curve fitting, undo/redo state transitions, spatial R-Tree queries, and binary file serialization.
2. **Golden Image Rendering Tests**: Renders test canvases into offscreen surfaces and compares PNG output against baseline golden images (pixel diff threshold < 0.5%).
3. **Fuzz Testing**: Uses LLVM `libFuzzer` to feed malformed `.obn` and PDF files into the engine file parser to prevent crash vulnerabilities.
