/**
 * 企业微信平板模式 Zygisk 模块
 *
 * 通过 Zygisk 内置 pltHookRegister 在 native 层 hook __system_property_get，
 * 不触碰 Java 层，不用 RegisterNatives，避免被检测。
 */

#include "zygisk.hpp"

#include <android/log.h>
#include <sys/system_properties.h>
#include <cstring>

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

// ─── PLT hook ─────────────────────────────────────────────────────────────────
static int (*orig_system_property_get)(const char *, char *) = nullptr;

static int my_system_property_get(const char *name, char *value) {
    const char *spoof = spoof_value(name);
    if (spoof) {
        strcpy(value, spoof);
        return (int)strlen(spoof);
    }
    return orig_system_property_get(name, value);
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

        LOGD("target process: com.tencent.wework");

        // Hook 所有已加载库（含 libandroid_runtime.so）中的 __system_property_get
        api->pltHookRegister(".*", "__system_property_get",
                             (void *)my_system_property_get,
                             (void **)&orig_system_property_get);
        if (!api->pltHookCommit()) {
            LOGE("pltHookCommit failed");
        } else {
            LOGD("plt hook installed");
        }
    }

private:
    zygisk::Api *api = nullptr;
    JNIEnv      *env = nullptr;
};

REGISTER_ZYGISK_MODULE(WxWorkTablet)
