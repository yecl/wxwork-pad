/**
 * 企业微信平板模式 Zygisk 模块
 *
 * 与 QQ/微信平板模式原模块完全相同的注入方式：
 *
 * 1. PLT hook __system_property_get
 *    在 preAppSpecialize 阶段修改进程内所有库的 PLT 表，
 *    覆盖所有 native 代码对系统属性的读取。
 *
 * 2. hookJniNativeMethods
 *    由 Zygisk 在 native 层直接 patch JNI 函数指针，
 *    效果等同于原模块的 my_native_get / orig_native_get，
 *    不调用 RegisterNatives，不修改 Java 方法表。
 */

#include "zygisk.hpp"

#include <android/log.h>
#include <sys/system_properties.h>
#include <cstring>
#include <jni.h>

#define LOG_TAG "WxWorkTablet"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─── 伪造的平板属性 ───────────────────────────────────────────────────────────
static constexpr char SPOOF_BRAND[]        = "HUAWEI";
static constexpr char SPOOF_MODEL[]        = "MatePad Pro";
static constexpr char SPOOF_MANUFACTURER[] = "HUAWEI";
static constexpr char SPOOF_MARKETNAME[]   = "HUAWEI MatePad Pro";

static const char *spoof_value(const char *key) {
    if (!key) return nullptr;
    if (strcmp(key, "ro.product.brand")        == 0) return SPOOF_BRAND;
    if (strcmp(key, "ro.product.model")        == 0) return SPOOF_MODEL;
    if (strcmp(key, "ro.product.manufacturer") == 0) return SPOOF_MANUFACTURER;
    if (strcmp(key, "ro.product.marketname")   == 0) return SPOOF_MARKETNAME;
    return nullptr;
}

// ─── Hook 1: __system_property_get（native 层 PLT hook）─────────────────────
static int (*orig_system_property_get)(const char *, char *) = nullptr;

static int my_system_property_get(const char *name, char *value) {
    const char *spoof = spoof_value(name);
    if (spoof) {
        strcpy(value, spoof);
        return (int)strlen(spoof);
    }
    return orig_system_property_get(name, value);
}

// ─── Hook 2: SystemProperties.native_get（Zygisk native patch）──────────────
// orig_native_get 由 hookJniNativeMethods 自动填入（写入 JNINativeMethod.fnPtr）
static jstring (*orig_native_get)(JNIEnv *, jclass, jstring, jstring) = nullptr;

static jstring my_native_get(JNIEnv *env, jclass clazz,
                              jstring keyJ, jstring defJ) {
    if (!keyJ) return orig_native_get(env, clazz, keyJ, defJ);

    const char *key = env->GetStringUTFChars(keyJ, nullptr);
    if (!key) return orig_native_get(env, clazz, keyJ, defJ);

    const char *spoof = spoof_value(key);
    env->ReleaseStringUTFChars(keyJ, key);

    if (spoof) return env->NewStringUTF(spoof);
    return orig_native_get(env, clazz, keyJ, defJ);
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
        bool is_target = name && strncmp(name, "com.tencent.wework", 18) == 0;
        if (name) env->ReleaseStringUTFChars(args->nice_name, name);

        if (!is_target) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        LOGD("target: com.tencent.wework");

        // Hook 1: PLT hook，覆盖所有已加载库（含 libandroid_runtime.so）
        api->pltHookRegister(".*", "__system_property_get",
                             (void *)my_system_property_get,
                             (void **)&orig_system_property_get);
        if (!api->pltHookCommit()) {
            LOGE("pltHookCommit failed");
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        // Hook 2: Zygisk native patch，等同于原模块的 my_native_get/orig_native_get
        // fnPtr 字段在调用后被 Zygisk 替换为原始函数指针
        JNINativeMethod methods[] = {{
            "native_get",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            (void *)my_native_get
        }};
        api->hookJniNativeMethods(env, "android/os/SystemProperties",
                                  methods, 1);
        // hookJniNativeMethods 调用后 methods[0].fnPtr 被改写为原始指针
        orig_native_get = (jstring(*)(JNIEnv *, jclass, jstring, jstring))
                          methods[0].fnPtr;
        LOGD("hooks installed");
    }

private:
    zygisk::Api *api = nullptr;
    JNIEnv      *env = nullptr;
};

REGISTER_ZYGISK_MODULE(WxWorkTablet)
