# 09 — File Format, Auto-Save & Crash Recovery Specification

## Overview

OpenBoard utilizes `.obn` (**O**pen**B**oard **N**ative) for persistent storage. To protect teachers against sudden power outages, accidental app closures, or device shutoffs, OpenBoard includes a background **Auto-Save & Sudden Shutdown Recovery System**.

---

## ⚡ Sudden Shutdown & Power Loss Protection Engine

```
                                  [App Launch]
                                       │
                                       ▼
                         Is .clean_exit flag missing AND
                         crash_recovery.obn.tmp exists?
                                       │
                    ┌──────────────────┴──────────────────┐
                 YES│                                   NO│
                    ▼                                     ▼
     ┌──────────────────────────────┐          ┌─────────────────────┐
     │ Show Recovery Dialog UI      │          │ Normal Startup Mode │
     │ "Unsaved Session Detected.   │          └─────────────────────┘
     │ Restore previous board?"     │
     └──────────────┬───────────────┘
                    │
            ┌───────┴───────┐
            │               │
      [RESTORE]         [DISCARD]
            │               │
            ▼               ▼
    Load setup from    Delete tmp file,
    tmp binary snapshot start clean board
```

---

## 🔒 Crash Recovery Lifecycle

1. **Session Start**:
   - Engine creates a session directory `[app_data]/recovery/`.
   - Removes `.clean_exit` marker file.
2. **Background Incremental Auto-Save**:
   - An asynchronous I/O thread writes canvas state snapshots to `crash_recovery.obn.tmp` at configurable intervals (default: **30 seconds** or after **10 stroke commits**).
   - Uses atomic double-buffering (`crash_recovery_a.tmp` / `crash_recovery_b.tmp`) to prevent file corruption during an active write when power goes out.
3. **Clean App Shutdown**:
   - When the user deliberately exits OpenBoard via the exit button, the engine saves the final `.obn` file, removes `crash_recovery.obn.tmp`, and writes `.clean_exit`.
4. **Sudden Shutdown / Power Cut**:
   - Power goes out or app crashes \(\rightarrow\) `.clean_exit` is **never written**, leaving `crash_recovery.obn.tmp` intact.
5. **Next App Launch Detection**:
   - Engine checks startup conditions. Finding `crash_recovery.obn.tmp` without `.clean_exit` triggers the **Session Recovery Dialog**.

---

## 💬 Session Recovery UI Dialog Specification

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
│    [ 🗑️ Discard & Start Fresh ]      [ 🔄 Restore Session ]   │
└──────────────────────────────────────────────────────────────┘
```

- **Restore Session**: Loads the binary setup from `crash_recovery.obn.tmp` directly into the active viewport with all pages, layers, ink strokes, shapes, and text intact.
- **Discard & Start Fresh**: Permanently deletes the temporary recovery snapshot and initializes a blank document canvas.
