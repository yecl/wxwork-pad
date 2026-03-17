/**
 * 企业微信平板模式 Zygisk 模块
 *
 * 注入流程：
 *   preAppSpecialize  → 确认目标进程，否则卸载
 *   postAppSpecialize → 初始化 LSPlant + ShadowHook，
 *                       加载内嵌 DEX，hook Application.onCreate
 *   Application.onCreate → 调用原始 onCreate，然后
 *                           hook WeworkServiceImpl.isAndroidPad*() 返回 true
 */

#include "zygisk.hpp"
#include <lsplant.hpp>
#include <shadowhook.h>

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <cstring>
#include <string>

// 由 GitHub Actions 编译 WxWorkCallback.java 后生成
#include "dex_bytes.h"

#define TAG "WxWorkTablet"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ─── 全局状态 ─────────────────────────────────────────────────────────────────
static jobject g_hooker_obj           = nullptr;  // WxWorkCallback 实例
static jobject g_ispad_method         = nullptr;  // reflected Method: isPad
static jobject g_app_oncreate_method  = nullptr;  // reflected Method: onAppCreate
static jobject g_app_backup           = nullptr;  // LSPlant backup for Application.onCreate

// ─── 前向声明 ─────────────────────────────────────────────────────────────────
static void hook_wework(JNIEnv *env);

// ─── JNI 桥接，供 WxWorkCallback 的 native 方法调用 ──────────────────────────

static void jni_call_original_on_create(JNIEnv *env, jobject /*thiz*/, jobject app) {
    if (!g_app_backup) return;
    jclass method_cls = env->FindClass("java/lang/reflect/Method");
    if (!method_cls) return;
    jmethodID invoke = env->GetMethodID(method_cls, "invoke",
        "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    env->CallObjectMethod(g_app_backup, invoke, app, nullptr);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static void jni_hook_wework(JNIEnv *env, jobject /*thiz*/) {
    hook_wework(env);
}

// ─── LSPlant 初始化 ───────────────────────────────────────────────────────────
static bool init_lsplant(JNIEnv *env) {
    static bool done = false;
    if (done) return true;

    void *libart = dlopen("libart.so", RTLD_NOW | RTLD_NOLOAD);
    if (!libart) libart = dlopen("libart.so", RTLD_NOW);
    if (!libart) { LOGE("dlopen libart.so failed"); return false; }

    lsplant::InitInfo info{
        .inline_hooker = [](void *target, void *hook) -> void * {
            void *backup = nullptr;
            return shadowhook_hook_func_addr(target, hook, &backup) == 0
                       ? backup : nullptr;
        },
        .inline_unhooker = [](void *backup) -> bool {
            return shadowhook_unhook(backup) == 0;
        },
        .art_symbol_resolver = [](std::string_view sym) -> void * {
            static void *h = dlopen("libart.so", RTLD_NOW | RTLD_NOLOAD);
            return dlsym(h, sym.data());
        },
        .art_symbol_prefix_resolver = [](std::string_view) -> void * {
            return nullptr;
        },
    };

    done = lsplant::Init(env, info);
    if (!done) LOGE("lsplant::Init failed");
    return done;
}

// ─── 从内嵌 DEX 加载 WxWorkCallback，注册 native 方法，准备回调 ──────────────
static bool prepare_hooker(JNIEnv *env) {
    if (g_hooker_obj) return true;

    // ByteBuffer.wrap(kHookerDex)
    jbyteArray arr = env->NewByteArray(kHookerDexSize);
    if (!arr) return false;
    env->SetByteArrayRegion(arr, 0, kHookerDexSize,
                            reinterpret_cast<const jbyte *>(kHookerDex));

    jclass  bb_cls = env->FindClass("java/nio/ByteBuffer");
    jobject bb     = env->CallStaticObjectMethod(bb_cls,
        env->GetStaticMethodID(bb_cls, "wrap", "([B)Ljava/nio/ByteBuffer;"), arr);
    env->DeleteLocalRef(arr);
    if (!bb) { LOGE("ByteBuffer.wrap failed"); return false; }

    jclass  cl_cls = env->FindClass("java/lang/ClassLoader");
    jobject parent = env->CallStaticObjectMethod(cl_cls,
        env->GetStaticMethodID(cl_cls, "getSystemClassLoader",
                               "()Ljava/lang/ClassLoader;"));

    // InMemoryDexClassLoader — API 26+
    jclass imdcl = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (!imdcl || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("InMemoryDexClassLoader not available (API < 26?)");
        return false;
    }
    jobject loader = env->NewObject(imdcl,
        env->GetMethodID(imdcl, "<init>",
                         "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V"),
        bb, parent);
    env->DeleteLocalRef(bb);
    env->DeleteLocalRef(parent);
    if (!loader) { LOGE("InMemoryDexClassLoader ctor failed"); return false; }

    // 加载 WxWorkCallback 类
    jstring cname  = env->NewStringUTF("WxWorkCallback");
    jclass  cb_cls = (jclass)env->CallObjectMethod(loader,
        env->GetMethodID(cl_cls, "loadClass",
                         "(Ljava/lang/String;)Ljava/lang/Class;"), cname);
    env->DeleteLocalRef(cname);
    env->DeleteLocalRef(loader);
    if (!cb_cls || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("loadClass WxWorkCallback failed");
        return false;
    }

    // 注册 native 方法
    JNINativeMethod natives[] = {
        {"nativeCallOriginalOnCreate", "(Ljava/lang/Object;)V",
         (void *)jni_call_original_on_create},
        {"nativeHookWework",           "()V",
         (void *)jni_hook_wework},
    };
    if (env->RegisterNatives(cb_cls, natives, 2) != 0) {
        env->ExceptionClear();
        LOGE("RegisterNatives failed");
        env->DeleteLocalRef(cb_cls);
        return false;
    }

    // 创建 WxWorkCallback 实例
    jobject inst = env->NewObject(cb_cls,
        env->GetMethodID(cb_cls, "<init>", "()V"));
    if (!inst) { LOGE("WxWorkCallback ctor failed"); env->DeleteLocalRef(cb_cls); return false; }

    // 获取两个回调方法的 reflected Method 对象
    jmethodID isPad_mid = env->GetMethodID(cb_cls, "isPad",
        "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID appCreate_mid = env->GetMethodID(cb_cls, "onAppCreate",
        "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    if (!isPad_mid || !appCreate_mid) {
        LOGE("callback methods not found in WxWorkCallback");
        env->DeleteLocalRef(inst);
        env->DeleteLocalRef(cb_cls);
        return false;
    }

    jobject ispad_refl      = env->ToReflectedMethod(cb_cls, isPad_mid,      JNI_FALSE);
    jobject appcreate_refl  = env->ToReflectedMethod(cb_cls, appCreate_mid,  JNI_FALSE);

    g_hooker_obj           = env->NewGlobalRef(inst);
    g_ispad_method         = env->NewGlobalRef(ispad_refl);
    g_app_oncreate_method  = env->NewGlobalRef(appcreate_refl);

    env->DeleteLocalRef(inst);
    env->DeleteLocalRef(ispad_refl);
    env->DeleteLocalRef(appcreate_refl);
    env->DeleteLocalRef(cb_cls);

    LOGD("hooker prepared");
    return true;
}

// ─── hook WeworkServiceImpl.isAndroidPad*() ──────────────────────────────────
static void hook_wework(JNIEnv *env) {
    // 通过当前线程的 ContextClassLoader 访问企业微信内部类
    jclass  thread_cls = env->FindClass("java/lang/Thread");
    jobject cur_thread = env->CallStaticObjectMethod(thread_cls,
        env->GetStaticMethodID(thread_cls, "currentThread", "()Ljava/lang/Thread;"));
    jobject app_cl = env->CallObjectMethod(cur_thread,
        env->GetMethodID(thread_cls, "getContextClassLoader",
                         "()Ljava/lang/ClassLoader;"));
    env->DeleteLocalRef(cur_thread);

    jclass  cl_cls  = env->FindClass("java/lang/ClassLoader");
    jstring cname   = env->NewStringUTF(
        "com.tencent.wework.foundation.impl.WeworkServiceImpl");
    jclass  svc_cls = (jclass)env->CallObjectMethod(app_cl,
        env->GetMethodID(cl_cls, "loadClass",
                         "(Ljava/lang/String;)Ljava/lang/Class;"), cname);
    env->DeleteLocalRef(cname);
    env->DeleteLocalRef(app_cl);

    if (!svc_cls || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("WeworkServiceImpl not found");
        return;
    }

    // 遍历所有方法，hook 名为 isAndroidPad* 的方法
    jclass       cls_cls    = env->FindClass("java/lang/Class");
    jclass       method_cls = env->FindClass("java/lang/reflect/Method");
    jobjectArray methods    = (jobjectArray)env->CallObjectMethod(svc_cls,
        env->GetMethodID(cls_cls, "getDeclaredMethods",
                         "()[Ljava/lang/reflect/Method;"));
    env->DeleteLocalRef(svc_cls);
    if (!methods) { LOGE("getDeclaredMethods failed"); return; }

    jmethodID get_name = env->GetMethodID(method_cls, "getName", "()Ljava/lang/String;");
    int hooked = 0;
    jint len   = env->GetArrayLength(methods);

    for (jint i = 0; i < len; i++) {
        jobject m      = env->GetObjectArrayElement(methods, i);
        jstring name_j = (jstring)env->CallObjectMethod(m, get_name);
        const char *name = env->GetStringUTFChars(name_j, nullptr);
        bool match = name && strncmp(name, "isAndroidPad", 12) == 0;
        env->ReleaseStringUTFChars(name_j, name);
        env->DeleteLocalRef(name_j);

        if (match) {
            jobject backup = lsplant::Hook(env, m, g_hooker_obj, g_ispad_method);
            if (backup) {
                env->NewGlobalRef(backup);  // 防止被 GC
                hooked++;
                LOGD("hooked isAndroidPad method #%d", i);
            } else {
                LOGE("lsplant::Hook failed for method #%d", i);
            }
        }
        env->DeleteLocalRef(m);
    }
    env->DeleteLocalRef(methods);
    LOGD("total hooked: %d", hooked);
}

// ─── Zygisk 模块 ──────────────────────────────────────────────────────────────
class WxWorkTablet : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        bool target = name && strncmp(name, "com.tencent.wework", 18) == 0;
        if (name) env->ReleaseStringUTFChars(args->nice_name, name);

        if (!target) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        } else {
            LOGD("target process: com.tencent.wework");
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!init_lsplant(env))   return;
        if (!prepare_hooker(env)) return;
        hook_application_oncreate();
    }

private:
    zygisk::Api *api = nullptr;
    JNIEnv      *env = nullptr;

    // Hook Application.onCreate；在回调里再 hook WeworkServiceImpl
    void hook_application_oncreate() {
        jclass app_cls = env->FindClass("android/app/Application");
        if (!app_cls) { LOGE("Application class not found"); return; }

        jclass  cls_cls  = env->FindClass("java/lang/Class");
        jstring method_n = env->NewStringUTF("onCreate");
        jobjectArray empty = env->NewObjectArray(0,
            env->FindClass("java/lang/Class"), nullptr);
        jobject on_create = env->CallObjectMethod(app_cls,
            env->GetMethodID(cls_cls, "getDeclaredMethod",
                "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;"),
            method_n, empty);
        env->DeleteLocalRef(method_n);
        env->DeleteLocalRef(empty);

        if (!on_create || env->ExceptionCheck()) {
            env->ExceptionClear();
            LOGE("Application.onCreate not found");
            env->DeleteLocalRef(app_cls);
            return;
        }

        g_app_backup = lsplant::Hook(env, on_create,
                                     g_hooker_obj, g_app_oncreate_method);
        if (g_app_backup) {
            g_app_backup = env->NewGlobalRef(g_app_backup);
            LOGD("Application.onCreate hooked");
        } else {
            LOGE("Application.onCreate hook failed");
        }
        env->DeleteLocalRef(on_create);
        env->DeleteLocalRef(app_cls);
    }
};

REGISTER_ZYGISK_MODULE(WxWorkTablet)
