package com.cruse.openwhiteboard.engine

/**
 * Kotlin interface implemented by the UI layer.
 * The C++ engine calls these methods via JNI from background threads.
 * All UI updates must be posted to the main thread.
 */
interface EngineCallbacksInterface {
    /** Engine needs a frame redrawn. Post Choreographer.postFrameCallback(). */
    fun onInvalidate()

    /** Document modified state changed (show/hide dirty indicator in title). */
    fun onDocumentDirtyChanged(isDirty: Boolean)

    /**
     * A crash recovery file was found on startup.
     * Show the recovery dialog to the user.
     */
    fun onRecoveryAvailable(snapshotPath: String, pageCount: Int, timestamp: Long)

    /** Background auto-save completed. */
    fun onAutoSaveComplete(success: Boolean)

    /** Page list changed (added, removed, reordered). Refresh page nav panel. */
    fun onPagesChanged()

    /** Undo/redo availability changed. Update toolbar button states. */
    fun onUndoRedoChanged(canUndo: Boolean, canRedo: Boolean)

    /** Native engine encountered an error. Show error to user. */
    fun onError(message: String)

    /** Selection state changed. Position floating context menu over bounds. */
    fun onSelectionChanged(hasSelection: Boolean, isLocked: Boolean, left: Float, top: Float, right: Float, bottom: Float)
}
