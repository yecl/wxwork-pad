/**
 * 企业微信平板模式 Zygisk 模块
 *
 * Hook android.os.SystemProperties.native_get（JNI 层），
 * 覆盖 Java 层 Build.MODEL / SystemProperties.get() 的调用路径。
 * 企业微信的平板检测走 Java 层，这一层足够。
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

// 根据 key 返回伪造值，不需要伪造则返回 nullptr
static const char *spoof_value(const char *key) {
    if (!key) return nullptr;
    if (strcmp(key, "ro.product.brand")        == 0) return SPOOF_BRAND;
    if (strcmp(key, "ro.product.model")        == 0) return SPOOF_MODEL;
    if (strcmp(key, "ro.product.manufacturer") == 0) return SPOOF_MANUFACTURER;
    if (strcmp(key, "ro.product.marketname")   == 0) return SPOOF_MARKETNAME;
    return nullptr;
}

// ─── JNI hook ─────────────────────────────────────────────────────────────────
// 挂钩 android.os.SystemProperties.native_get
// 覆盖 Java 层 Build.MODEL / SystemProperties.get() 的调用路径

static jstring my_native_get(JNIEnv *env, jclass, jstring keyJ, jstring defJ) {
    if (!keyJ) return defJ;

    const char *key = env->GetStringUTFChars(keyJ, nullptr);
    if (!key) return defJ;

    const char *spoof = spoof_value(key);
    env->ReleaseStringUTFChars(keyJ, key);

    if (spoof) return env->NewStringUTF(spoof);

    // 未命中：回落到真实值（直接调 libc，不经过被我们 PLT-hook 的路径）
    const char *key2 = env->GetStringUTFChars(keyJ, nullptr);
    char buf[PROP_VALUE_MAX] = {};
    if (key2) {
        __system_property_get(key2, buf);
        env->ReleaseStringUTFChars(keyJ, key2);
    }
    return buf[0] ? env->NewStringUTF(buf) : defJ;
}

// ─── Zygisk 模块主体 ──────────────────────────────────────────────────────────

class WxWorkTablet : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // 检查进程名
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        bool is_target = name && strncmp(name, "com.tencent.wework", 18) == 0;
        if (name) env->ReleaseStringUTFChars(args->nice_name, name);

        if (!is_target) {
            // 不是目标进程，立即卸载，不留任何 hook
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        LOGD("target process: com.tencent.wework");
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        // Layer 2：JNI 替换，覆盖 Java 层路径
        // hookJniNativeMethods 同时保存原始指针（写入 methods[i].fnPtr）
        JNINativeMethod methods[] = {
            {
                "native_get",
                "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                (void *)my_native_get
            },
        };
        api->hookJniNativeMethods(env, "android/os/SystemProperties",
                                  methods, 1);
        LOGD("JNI hook installed");
    }

private:
    zygisk::Api *api = nullptr;
    JNIEnv      *env = nullptr;
};

REGISTER_ZYGISK_MODULE(WxWorkTablet)
