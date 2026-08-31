#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <android/bitmap.h>
#include "ob/WhiteboardEngine.h"
#include "ob/GlesRenderPipeline.h"  // Android-specific — only this file knows about GLES
#include "ob/InputSystem.h"
#include "ob/Tools.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

#define LOG_TAG "OB_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::mutex g_engineMutex;
static std::unordered_map<jlong, std::unique_ptr<ob::WhiteboardEngine>> g_engines;
static jlong g_nextId = 1;

static ob::WhiteboardEngine* getEngine(jlong handle) {
    std::lock_guard<std::mutex> lock(g_engineMutex);
    auto it = g_engines.find(handle);
    return it != g_engines.end() ? it->second.get() : nullptr;
}

static std::string jstring2str(JNIEnv* env, jstring js) {
    if (!js) return {};
    const char* c = env->GetStringUTFChars(js, nullptr);
    std::string s = c ? c : "";
    if (c) env->ReleaseStringUTFChars(js, c);
    return s;
}

static JavaVM* g_jvm = nullptr;
static jobject g_callbackObj = nullptr;

static jmethodID g_midInvalidate          = nullptr;
static jmethodID g_midDocDirtyChanged     = nullptr;
static jmethodID g_midRecoveryAvailable   = nullptr;
static jmethodID g_midAutoSaveComplete    = nullptr;
static jmethodID g_midPagesChanged        = nullptr;
static jmethodID g_midUndoRedoChanged     = nullptr;
static jmethodID g_midOnError             = nullptr;
static jmethodID g_midSelectionChanged    = nullptr;

static void callJavaVoid(jmethodID mid) {
    if (!g_jvm || !g_callbackObj || !mid) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    env->CallVoidMethod(g_callbackObj, mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) g_jvm->DetachCurrentThread();
}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_jvm = vm;
    LOGI("JNI_OnLoad: OpenWhiteBoard Native Engine");
    return JNI_VERSION_1_6;
}

JNIEXPORT jlong JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeCreate(
        JNIEnv* env, jclass, jstring jFilesDir)
{
    ob::EngineConfig cfg;
    cfg.autoSavePath        = jstring2str(env, jFilesDir);
    cfg.autoSaveIntervalSec = 30;
    cfg.undoHistoryLimit    = 50;
    cfg.palmRejection       = true;
    cfg.enableKalmanFilter  = true;

    auto engine = std::make_unique<ob::WhiteboardEngine>(cfg);

    std::lock_guard<std::mutex> lock(g_engineMutex);
    jlong id = g_nextId++;
    g_engines[id] = std::move(engine);
    LOGI("Engine created, handle=%lld", (long long)id);
    return id;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetCallbacks(
        JNIEnv* env, jclass, jlong handle, jobject callbackObj)
{
    auto* engine = getEngine(handle);
    if (!engine) return;

    if (g_callbackObj) {
        env->DeleteGlobalRef(g_callbackObj);
        g_callbackObj = nullptr;
    }
    if (callbackObj) {
        g_callbackObj = env->NewGlobalRef(callbackObj);
    } else {
        return;
    }

    jclass interfaceCls = env->FindClass("com/cruse/openwhiteboard/engine/EngineCallbacksInterface");
    if (!interfaceCls) {
        LOGE("Could not find EngineCallbacksInterface class via JNI");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }

    g_midInvalidate        = env->GetMethodID(interfaceCls, "onInvalidate",        "()V");
    g_midDocDirtyChanged   = env->GetMethodID(interfaceCls, "onDocumentDirtyChanged","(Z)V");
    g_midRecoveryAvailable = env->GetMethodID(interfaceCls, "onRecoveryAvailable",  "(Ljava/lang/String;IJ)V");
    g_midAutoSaveComplete  = env->GetMethodID(interfaceCls, "onAutoSaveComplete",   "(Z)V");
    g_midPagesChanged      = env->GetMethodID(interfaceCls, "onPagesChanged",       "()V");
    g_midUndoRedoChanged   = env->GetMethodID(interfaceCls, "onUndoRedoChanged",    "(ZZ)V");
    g_midOnError           = env->GetMethodID(interfaceCls, "onError",              "(Ljava/lang/String;)V");
    g_midSelectionChanged  = env->GetMethodID(interfaceCls, "onSelectionChanged",   "(ZZFFFF)V");

    if (env->ExceptionCheck()) {
        LOGE("Cleared JNI exception during GetMethodID");
        env->ExceptionClear();
    }

    ob::EngineCallbacks cbs;
    cbs.onInvalidate = []{
        callJavaVoid(g_midInvalidate);
    };
    cbs.onDocumentDirtyChanged = [](bool dirty) {
        if (!g_jvm || !g_callbackObj || !g_midDocDirtyChanged) return;
        JNIEnv* e = nullptr; bool att = false;
        if (g_jvm->GetEnv((void**)&e, JNI_VERSION_1_6) != JNI_OK) {
            if (g_jvm->AttachCurrentThread(&e, nullptr) != JNI_OK) return;
            att = true;
        }
        e->CallVoidMethod(g_callbackObj, g_midDocDirtyChanged, (jboolean)dirty);
        if (e->ExceptionCheck()) e->ExceptionClear();
        if (att) g_jvm->DetachCurrentThread();
    };
    cbs.onPagesChanged = [] {
        callJavaVoid(g_midPagesChanged);
    };
    cbs.onUndoRedoChanged = [](bool canUndo, bool canRedo) {
        if (!g_jvm || !g_callbackObj || !g_midUndoRedoChanged) return;
        JNIEnv* e = nullptr; bool att = false;
        if (g_jvm->GetEnv((void**)&e, JNI_VERSION_1_6) != JNI_OK) {
            if (g_jvm->AttachCurrentThread(&e, nullptr) != JNI_OK) return;
            att = true;
        }
        e->CallVoidMethod(g_callbackObj, g_midUndoRedoChanged, (jboolean)canUndo, (jboolean)canRedo);
        if (e->ExceptionCheck()) e->ExceptionClear();
        if (att) g_jvm->DetachCurrentThread();
    };
    cbs.onError = [](const std::string& msg) {
        if (!g_jvm || !g_callbackObj || !g_midOnError) return;
        JNIEnv* e = nullptr; bool att = false;
        if (g_jvm->GetEnv((void**)&e, JNI_VERSION_1_6) != JNI_OK) {
            if (g_jvm->AttachCurrentThread(&e, nullptr) != JNI_OK) return;
            att = true;
        }
        jstring jmsg = e->NewStringUTF(msg.c_str());
        e->CallVoidMethod(g_callbackObj, g_midOnError, jmsg);
        if (jmsg) e->DeleteLocalRef(jmsg);
        if (e->ExceptionCheck()) e->ExceptionClear();
        if (att) g_jvm->DetachCurrentThread();
    };
    cbs.onSelectionChanged = [](bool hasSel, bool isLocked, float l, float t, float r, float b) {
        if (!g_jvm || !g_callbackObj || !g_midSelectionChanged) return;
        JNIEnv* e = nullptr; bool att = false;
        if (g_jvm->GetEnv((void**)&e, JNI_VERSION_1_6) != JNI_OK) {
            if (g_jvm->AttachCurrentThread(&e, nullptr) != JNI_OK) return;
            att = true;
        }
        e->CallVoidMethod(g_callbackObj, g_midSelectionChanged, (jboolean)hasSel, (jboolean)isLocked, l, t, r, b);
        if (e->ExceptionCheck()) e->ExceptionClear();
        if (att) g_jvm->DetachCurrentThread();
    };

    engine->setCallbacks(std::move(cbs));
    LOGI("Callbacks successfully configured for engine handle=%lld", (long long)handle);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeDestroy(
        JNIEnv*, jclass, jlong handle)
{
    std::lock_guard<std::mutex> lock(g_engineMutex);
    g_engines.erase(handle);
    LOGI("Engine destroyed, handle=%lld", (long long)handle);
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSurfaceCreated(
        JNIEnv* env, jclass, jlong handle, jobject surface, jint w, jint h)
{
    auto* engine = getEngine(handle);
    if (!engine || !surface) return JNI_FALSE;
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LOGE("ANativeWindow_fromSurface returned null");
        return JNI_FALSE;
    }
    // The JNI bridge is the ONLY place that knows about Android GLES.
    // Engine core only depends on IRenderPipeline*.
    auto* glesRenderer = new ob::GlesRenderPipeline();
    bool ok = engine->initRenderer(glesRenderer, window, w, h);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSurfaceChanged(
        JNIEnv*, jclass, jlong handle, jint w, jint h)
{
    auto* engine = getEngine(handle);
    if (engine) engine->onSurfaceChanged(w, h);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSurfaceDestroyed(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->destroyRenderer();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeRenderFrame(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->renderFrame();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeTouchEvent(
        JNIEnv* env, jclass, jlong handle,
        jint actionIndex, jboolean isPen,
        jintArray jIds, jfloatArray jXs, jfloatArray jYs,
        jfloatArray jPressures, jfloatArray jMajors,
        jintArray jPhases, jlong timestamp)
{
    auto* engine = getEngine(handle);
    if (!engine || !jIds || !jXs || !jYs) return;

    jsize count = env->GetArrayLength(jIds);
    if (count <= 0) return;

    jint*   ids       = env->GetIntArrayElements(jIds, nullptr);
    jfloat* xs        = env->GetFloatArrayElements(jXs, nullptr);
    jfloat* ys        = env->GetFloatArrayElements(jYs, nullptr);
    jfloat* pressures = jPressures ? env->GetFloatArrayElements(jPressures, nullptr) : nullptr;
    jfloat* majors    = jMajors    ? env->GetFloatArrayElements(jMajors, nullptr)    : nullptr;
    jint*   phases    = jPhases   ? env->GetIntArrayElements(jPhases, nullptr)      : nullptr;

    ob::TouchEvent event;
    event.actionIndex = actionIndex;
    event.isPen       = (bool)isPen;

    for (jsize i = 0; i < count; i++) {
        ob::TouchPointer p;
        p.pointerId  = ids ? ids[i] : 0;
        p.x          = xs  ? xs[i]  : 0.f;
        p.y          = ys  ? ys[i]  : 0.f;
        p.pressure   = pressures ? pressures[i] : 1.0f;
        p.touchMajor = majors    ? majors[i]    : 10.0f;
        p.touchMinor = p.touchMajor;
        p.timestamp  = (int64_t)timestamp;
        p.phase      = phases ? (ob::TouchPhase)phases[i] : ob::TouchPhase::MOVED;
        p.isPen      = (bool)isPen;
        event.pointers.push_back(p);
    }

    if (ids)       env->ReleaseIntArrayElements(jIds,       ids,       JNI_ABORT);
    if (xs)        env->ReleaseFloatArrayElements(jXs,      xs,        JNI_ABORT);
    if (ys)        env->ReleaseFloatArrayElements(jYs,      ys,        JNI_ABORT);
    if (pressures) env->ReleaseFloatArrayElements(jPressures,pressures,JNI_ABORT);
    if (majors)    env->ReleaseFloatArrayElements(jMajors,  majors,    JNI_ABORT);
    if (phases)    env->ReleaseIntArrayElements(jPhases,    phases,    JNI_ABORT);

    engine->onTouchEvent(event);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetActiveTool(
        JNIEnv*, jclass, jlong handle, jint toolIndex)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setActiveTool((ob::ToolType)toolIndex);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetStrokeColor(
        JNIEnv*, jclass, jlong handle, jint argb)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setStrokeColor(ob::Color::fromARGB((uint32_t)argb));
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetStrokeWidth(
        JNIEnv*, jclass, jlong handle, jfloat width)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setStrokeWidth(width);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetStrokeOpacity(
        JNIEnv*, jclass, jlong handle, jfloat opacity)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setStrokeOpacity(opacity);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetPenType(
        JNIEnv*, jclass, jlong handle, jint penType)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setPenType((uint8_t)penType);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetEraserSize(
        JNIEnv*, jclass, jlong handle, jfloat size)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setEraserSize(size);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetEraserMode(
        JNIEnv*, jclass, jlong handle, jint mode)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setEraserMode((uint8_t)mode);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetShapeType(
        JNIEnv*, jclass, jlong handle, jint shapeType)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setShapeType((int32_t)shapeType);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeDuplicateSelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->duplicateSelected();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeCopySelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->copySelected();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativePaste(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->paste();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativePasteAt(
        JNIEnv*, jclass, jlong handle, jfloat x, jfloat y)
{
    auto* engine = getEngine(handle);
    if (engine) engine->pasteAt(x, y);
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeHasClipboard(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    return (engine && engine->hasClipboard()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetSelectedColor(
        JNIEnv*, jclass, jlong handle, jint color)
{
    auto* engine = getEngine(handle);
    if (engine) {
        uint32_t argb = (uint32_t)color;
        ob::Color c{
            static_cast<uint8_t>((argb >> 16) & 0xFF),
            static_cast<uint8_t>((argb >>  8) & 0xFF),
            static_cast<uint8_t>( argb        & 0xFF),
            static_cast<uint8_t>((argb >> 24) & 0xFF)
        };
        engine->setSelectedColor(c);
    }
}

JNIEXPORT jint JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeGetSelectedColor(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) {
        ob::Color c = engine->getSelectedColor();
        return (jint)(((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b);
    }
    return (jint)0xFF000000;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeLockSelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->lockSelected();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeToggleFillSelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->toggleFillSelected();
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeHasSelectedFilledShape(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    return (engine && engine->hasSelectedFilledShape()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeHasSelectedShape(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    return (engine && engine->hasSelectedShape()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeClearSelection(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->tools().selectionTool()->clearSelection();
}

JNIEXPORT jfloatArray JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeCanvasToScreenRect(
        JNIEnv* env, jclass, jlong handle, jfloat l, jfloat t, jfloat r, jfloat b)
{
    auto* engine = getEngine(handle);
    if (!engine) return nullptr;
    ob::Rectf s = engine->camera().canvasToScreenRect({l, t, r, b});
    jfloatArray arr = env->NewFloatArray(4);
    if (!arr) return nullptr;
    jfloat vals[4] = {s.left, s.top, s.right, s.bottom};
    env->SetFloatArrayRegion(arr, 0, 4, vals);
    return arr;
}

JNIEXPORT jfloatArray JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeScreenToCanvas(
        JNIEnv* env, jclass, jlong handle, jfloat sx, jfloat sy)
{
    auto* engine = getEngine(handle);
    if (!engine) return nullptr;
    ob::Vec2f c = engine->camera().screenToCanvas({sx, sy});
    jfloatArray arr = env->NewFloatArray(2);
    if (!arr) return nullptr;
    jfloat vals[2] = {c.x, c.y};
    env->SetFloatArrayRegion(arr, 0, 2, vals);
    return arr;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeNewDocument(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->newDocument();
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSaveDocument(
        JNIEnv* env, jclass, jlong handle, jstring jPath)
{
    auto* engine = getEngine(handle);
    if (!engine) return JNI_FALSE;
    return engine->saveDocument(jstring2str(env, jPath)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeOpenDocument(
        JNIEnv* env, jclass, jlong handle, jstring jPath)
{
    auto* engine = getEngine(handle);
    if (!engine) return JNI_FALSE;
    return engine->openDocument(jstring2str(env, jPath)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeExportPdf(
        JNIEnv* env, jclass, jlong handle, jstring jPath)
{
    auto* engine = getEngine(handle);
    if (!engine) return JNI_FALSE;
    return engine->exportPdf(jstring2str(env, jPath)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeAddPage(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->addPage();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeInsertPage(
        JNIEnv*, jclass, jlong handle, jint index)
{
    auto* engine = getEngine(handle);
    if (engine) engine->insertPage((size_t)index);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeDuplicatePage(
        JNIEnv*, jclass, jlong handle, jint index)
{
    auto* engine = getEngine(handle);
    if (engine) engine->duplicatePage((size_t)index);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeDeletePage(
        JNIEnv*, jclass, jlong handle, jint index)
{
    auto* engine = getEngine(handle);
    if (engine) engine->deletePage((size_t)index);
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeRenderPageThumbnail(
        JNIEnv* env, jclass, jlong handle, jint pageIndex, jobject jBitmap, jboolean clearBackground)
{
    auto* engine = getEngine(handle);
    if (!engine || !jBitmap) return JNI_FALSE;

    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, jBitmap, &info) < 0) return JNI_FALSE;
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) return JNI_FALSE;

    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, jBitmap, &pixels) < 0) return JNI_FALSE;

    bool ok = engine->renderPageThumbnail((size_t)pageIndex, pixels, (int32_t)info.width, (int32_t)info.height, clearBackground == JNI_TRUE);

    AndroidBitmap_unlockPixels(env, jBitmap);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetActivePage(
        JNIEnv*, jclass, jlong handle, jint index)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setActivePage(index);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeClearActivePage(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->clearActivePage();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetPageBackgroundColor(
        JNIEnv*, jclass, jlong handle, jint argb)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setActivePageBackgroundColor(ob::Color::fromARGB((uint32_t)argb));
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetPageGridType(
        JNIEnv*, jclass, jlong handle, jint gridType)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setActivePageGridType(gridType);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetAllPagesBackground(
        JNIEnv*, jclass, jlong handle, jint argb, jint gridType)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setAllPagesBackground(ob::Color::fromARGB((uint32_t)argb), gridType);
}

JNIEXPORT jint JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeGetPageCount(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    return engine ? (jint)engine->pageCount() : 0;
}

JNIEXPORT jint JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeGetActivePageIndex(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    return engine ? (jint)engine->activePageIndex() : 0;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeReorderPage(
        JNIEnv*, jclass, jlong handle, jint fromIdx, jint toIdx)
{
    auto* engine = getEngine(handle);
    if (engine) engine->reorderPage(fromIdx, toIdx);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeUndo(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->undo();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeRedo(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->redo();
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeCanUndo(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    return engine && engine->canUndo() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeCanRedo(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    return engine && engine->canRedo() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeFitPageToScreen(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->fitPageToScreen();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeZoomTo(
        JNIEnv*, jclass, jlong handle, jfloat factor, jfloat focusX, jfloat focusY)
{
    auto* engine = getEngine(handle);
    if (engine) engine->zoomTo(factor, {focusX, focusY});
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeHasRecovery(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    return engine && engine->hasRecoveryFile() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeRecoverSession(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    return engine && engine->recoverSession() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeDiscardRecovery(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->discardRecovery();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeDeleteSelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->deleteSelected();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeFlipHorizontalSelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->flipHorizontalSelected();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeFlipVerticalSelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->flipVerticalSelected();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeBringToFrontSelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->bringToFrontSelected();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSendBackwardSelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->sendBackwardSelected();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSendToBackSelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->sendToBackSelected();
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeBringForwardSelected(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) engine->bringForwardSelected();
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeTapSelectAt(
        JNIEnv*, jclass, jlong handle, jfloat screenX, jfloat screenY)
{
    auto* engine = getEngine(handle);
    if (!engine) return JNI_FALSE;
    // Convert screen → canvas coordinate
    ob::Vec2f canvasPt = engine->camera().screenToCanvas({screenX, screenY});
    bool hit = engine->tools().selectionTool()->tapSelectAt(canvasPt);
    return hit ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeUploadTexture(
        JNIEnv* env, jclass, jlong handle, jobject byteBuffer, jint width, jint height)
{
    auto* engine = getEngine(handle);
    if (!engine || !byteBuffer || width <= 0 || height <= 0) return 0;
    const uint8_t* pixels = (const uint8_t*)env->GetDirectBufferAddress(byteBuffer);
    if (!pixels) return 0;
    return (jint)engine->uploadTexture(pixels, width, height, true);
}

JNIEXPORT jint JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeAddImage(
        JNIEnv* env, jclass, jlong handle, jstring jPath, jint textureId,
        jfloat posX, jfloat posY, jfloat width, jfloat height)
{
    auto* engine = getEngine(handle);
    if (!engine) return 0;
    std::string path = jstring2str(env, jPath);
    return (jint)engine->addImage(path, (uint32_t)textureId, posX, posY, width, height);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetPageBackgroundTexture(
        JNIEnv* env, jclass, jlong handle, jint pageIndex, jint textureId, jstring jImagePath)
{
    auto* engine = getEngine(handle);
    if (engine) {
        std::string imgPath = jImagePath ? jstring2str(env, jImagePath) : "";
        engine->setPageBackgroundTexture((size_t)pageIndex, (uint32_t)textureId, imgPath);
    }
}

JNIEXPORT jint JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeGetPageBackgroundTexture(
        JNIEnv*, jclass, jlong handle, jint pageIndex)
{
    auto* engine = getEngine(handle);
    if (!engine) return 0;
    return (jint)engine->getPageBackgroundTexture((size_t)pageIndex);
}

JNIEXPORT jstring JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeGetPageBackgroundPath(
        JNIEnv* env, jclass, jlong handle, jint pageIndex)
{
    auto* engine = getEngine(handle);
    if (!engine) return env->NewStringUTF("");
    std::string path = engine->getPageBackgroundPath((size_t)pageIndex);
    return env->NewStringUTF(path.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeGetPageExportJson(
        JNIEnv* env, jclass, jlong handle, jint pageIndex)
{
    auto* engine = getEngine(handle);
    if (!engine) return env->NewStringUTF("{}");
    std::string jsonStr = engine->getPageExportJson((size_t)pageIndex);
    return env->NewStringUTF(jsonStr.c_str());
}

JNIEXPORT jint JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeAddImageToPage(
        JNIEnv* env, jclass, jlong handle, jint pageIndex, jstring jPath, jint textureId,
        jfloat posX, jfloat posY, jfloat width, jfloat height)
{
    auto* engine = getEngine(handle);
    if (!engine) return 0;
    std::string path = jstring2str(env, jPath);
    return (jint)engine->addImageToPage((size_t)pageIndex, path, (uint32_t)textureId, posX, posY, width, height);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetPageDimensions(
        JNIEnv*, jclass, jlong handle, jint pageIndex, jfloat width, jfloat height)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setPageDimensions((size_t)pageIndex, width, height);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeSetAllPagesDimensions(
        JNIEnv*, jclass, jlong handle, jfloat width, jfloat height)
{
    auto* engine = getEngine(handle);
    if (engine) engine->setAllPagesDimensions(width, height);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeResetDocumentPages(
        JNIEnv*, jclass, jlong handle, jint count, jfloat width, jfloat height)
{
    auto* engine = getEngine(handle);
    if (engine) engine->resetDocumentPages((size_t)count, width, height);
}

JNIEXPORT void JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeInsertPages(
        JNIEnv*, jclass, jlong handle, jint insertIndex, jint count, jfloat width, jfloat height)
{
    auto* engine = getEngine(handle);
    if (engine) engine->insertPages((size_t)insertIndex, (size_t)count, width, height);
}

JNIEXPORT jboolean JNICALL
Java_com_cruse_openwhiteboard_engine_EngineJNI_nativeIsDocumentEmpty(
        JNIEnv*, jclass, jlong handle)
{
    auto* engine = getEngine(handle);
    if (engine) return engine->isDocumentEmpty() ? JNI_TRUE : JNI_FALSE;
    return JNI_TRUE;
}

} // extern "C"
