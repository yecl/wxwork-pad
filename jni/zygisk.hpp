/*
 * Zygisk API — https://github.com/topjohnwu/Zygisk-module-sample
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is the official Zygisk API header from the sample module project.
 * Copy of the header at API version 4 (Magisk 26.x+).
 */
#pragma once

#include <jni.h>
#include <cstdint>
#include <cstring>

namespace zygisk {

struct AppSpecializeArgs {
    // Required
    jint &uid;
    jint &gid;
    jintArray &gids;
    jint &runtime_flags;
    jint &mount_external;
    jstring &se_info;
    jstring &nice_name;
    jstring &instruction_set;
    jstring &app_data_dir;
    // Optional — may be null depending on Android version
    jboolean *const is_child_zygote;
    jboolean *const is_top_app;
    jobjectArray *const pkg_data_info_list;
    jobjectArray *const whitelisted_data_info_list;
    jboolean *const mount_data_dirs;
    jboolean *const mount_storage_dirs;

    AppSpecializeArgs() = delete;
};

struct ServerSpecializeArgs {
    jint &uid;
    jint &gid;
    jintArray &gids;
    jint &runtime_flags;
    jlong &permitted_capabilities;
    jlong &effective_capabilities;

    ServerSpecializeArgs() = delete;
};

class Api;
class ModuleBase;

namespace internal {
struct api_table;
template<class T>
void entry_impl(api_table *, JNIEnv *);
} // namespace internal

enum Option : int {
    // Unload this module library after all "pre" stage callbacks are done.
    DLCLOSE_MODULE_LIBRARY = 0,
    // Allow the zygote process to access the module's companion socket.
    PROCESS_GRANT_ROOT = 1,
    // Allow the zygote process to use the companion socket with PROCESS_GRANT_ROOT.
    PROCESS_ON_DOMAIN_SOCKET = 2,
};

enum StateFlag : uint32_t {
    // The process is created by the main Zygote, not other Zygote variants.
    PROCESS_IS_EMULATOR         = (1u << 0),
    // Zygote was forked from a server (only in server context).
    SERVER_FORKED_FROM_ZYGOTE   = (1u << 1),
};

class Api final {
public:
    // Tell Zygisk what options to use.
    void setOption(Option opt);
    // Hook JNI native methods on a class. Must be called in postAppSpecialize.
    void hookJniNativeMethods(JNIEnv *env, const char *className,
                              JNINativeMethod *methods, int numMethods);
    // Register a PLT function hook on all loaded libraries matching 'regex'.
    void pltHookRegister(const char *regex, const char *symbol,
                         void *callback, void **backup);
    // Exclude a symbol from PLT hooking in libraries matching 'regex'.
    void pltHookExclude(const char *regex, const char *symbol);
    // Commit all registered PLT hooks. Must be called after pltHookRegister.
    bool pltHookCommit();
    // Connect to the companion process socket. Returns fd, or -1 on failure.
    int connectCompanion();
    // Get a dirfd of the module's private directory. Returns -1 on failure.
    int getModuleDir();
    // Get the current state flags.
    uint32_t getFlags();
    // Exempt a file descriptor from being closed during process specialization.
    bool exemptFd(int fd);

private:
    internal::api_table *tbl;
    template<class T>
    friend void internal::entry_impl(internal::api_table *, JNIEnv *);
};

class ModuleBase {
public:
    virtual void onLoad(Api *api, JNIEnv *env) {}
    virtual void preAppSpecialize(AppSpecializeArgs *args) {}
    virtual void postAppSpecialize(const AppSpecializeArgs *args) {}
    virtual void preServerSpecialize(ServerSpecializeArgs *args) {}
    virtual void postServerSpecialize(const ServerSpecializeArgs *args) {}
    virtual ~ModuleBase() = default;
};

#define REGISTER_ZYGISK_MODULE(clazz)                                           \
    __attribute__((visibility("default")))                                      \
    extern "C" void zygisk_module_entry(                                        \
            zygisk::internal::api_table *table, JNIEnv *env) {                  \
        zygisk::internal::entry_impl<clazz>(table, env);                        \
    }

// ─── Internal implementation details ─────────────────────────────────────────

namespace internal {

struct api_table {
    // slot 0
    void *impl;
    // slot 1 — registerModule, called by entry_impl
    bool (*registerModule)(api_table *, ModuleBase **);
    // slot 2
    void (*hookJniNativeMethods)(JNIEnv *, const char *, JNINativeMethod *, int);
    // slot 3
    void (*pltHookRegister)(void *, uint32_t, const char *, const char *, void *, void **);
    // slot 4
    void (*pltHookExclude)(void *, uint32_t, const char *, const char *);
    // slot 5
    bool (*pltHookCommit)(void *);
    // slot 6
    int  (*connectCompanion)(void *);
    // slot 7
    void (*setOption)(void *, Option);
    // slot 8
    int  (*getModuleDir)(void *);
    // slot 9
    uint32_t (*getFlags)(void *);
    // slot 10
    bool (*exemptFd)(void *, int);
};

template<class T>
void entry_impl(api_table *table, JNIEnv *env) {
    ModuleBase *module = new T();
    if (!table->registerModule(table, &module)) return;
    auto *api = new Api();
    api->tbl = table;
    module->onLoad(api, env);
}

} // namespace internal

// ─── Api inline implementations ──────────────────────────────────────────────

inline void Api::setOption(Option opt) {
    tbl->setOption(tbl->impl, opt);
}

inline void Api::hookJniNativeMethods(JNIEnv *env, const char *cls,
                                       JNINativeMethod *methods, int n) {
    tbl->hookJniNativeMethods(env, cls, methods, n);
}

inline void Api::pltHookRegister(const char *regex, const char *symbol,
                                   void *callback, void **backup) {
    tbl->pltHookRegister(tbl->impl, ~0u, regex, symbol, callback, backup);
}

inline void Api::pltHookExclude(const char *regex, const char *symbol) {
    tbl->pltHookExclude(tbl->impl, ~0u, regex, symbol);
}

inline bool Api::pltHookCommit() {
    return tbl->pltHookCommit(tbl->impl);
}

inline int Api::connectCompanion() {
    return tbl->connectCompanion(tbl->impl);
}

inline int Api::getModuleDir() {
    return tbl->getModuleDir(tbl->impl);
}

inline uint32_t Api::getFlags() {
    return tbl->getFlags(tbl->impl);
}

inline bool Api::exemptFd(int fd) {
    return tbl->exemptFd(tbl->impl, fd);
}

} // namespace zygisk
