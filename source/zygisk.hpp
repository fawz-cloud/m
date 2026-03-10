/* Zygisk Header - v4 API */
#pragma once
#include <jni.h>
#include <sys/types.h>

#define ZYGISK_API_VERSION 4

namespace zygisk {

enum Option { FORCE_DENYLIST_UNMOUNT = 0, DLCLOSE_MODULE_LIBRARY = 1, };
enum StateFlag : uint32_t { PROCESS_GRANTED_ROOT = (1u << 0), PROCESS_ON_DENYLIST = (1u << 1), };

struct AppSpecializeArgs {
    jint &uid; jint &gid; jintArray &gids; jint &runtime_flags; jobjectArray &rlimits;
    jint &mount_external; jstring &se_info; jstring &nice_name; jstring &instruction_set;
    jstring &app_data_dir; jboolean &is_child_zygote; jboolean &is_top_app;
    jobjectArray &pkg_data_info_list; jobjectArray &whitelisted_data_info_list;
    jboolean &mount_data_dirs; jboolean &mount_storage_dirs;
};

struct ServerSpecializeArgs {
    jint &uid; jint &gid; jintArray &gids; jint &runtime_flags;
    jlong &permitted_capabilities; jlong &effective_capabilities;
};

class Api;

class ModuleBase {
public:
    virtual void onLoad([[maybe_unused]] Api *api, [[maybe_unused]] JNIEnv *env) {}
    virtual void preAppSpecialize([[maybe_unused]] AppSpecializeArgs *args) {}
    virtual void postAppSpecialize([[maybe_unused]] const AppSpecializeArgs *args) {}
    virtual void preServerSpecialize([[maybe_unused]] ServerSpecializeArgs *args) {}
    virtual void postServerSpecialize([[maybe_unused]] const ServerSpecializeArgs *args) {}
    virtual ~ModuleBase() = default;
};

class Api {
public:
    void setOption(Option opt);
    bool getFlags(uint32_t *flags);
    void hookJniNativeMethods(JNIEnv *env, const char *className, JNINativeMethod *methods, int numMethods);
    int connectCompanion();
    void pltHookRegister(dev_t dev, ino_t inode, const char *symbol, void *newFunc, void **oldFunc);
    void pltHookExclude(dev_t dev, ino_t inode, const char *symbol);
    bool pltHookCommit();
};

} // namespace zygisk

#define REGISTER_ZYGISK_MODULE(clazz) \
    void zygisk_module_entry(zygisk::Api *api, JNIEnv *env) { \
        static clazz module; module.onLoad(api, env); \
    } \
    extern "C" [[gnu::visibility("default")]] void \
    zygisk_module_entry_impl(void *api, JNIEnv *env) { \
        zygisk_module_entry(reinterpret_cast<zygisk::Api *>(api), env); \
    }

#define REGISTER_ZYGISK_COMPANION(func) \
    extern "C" [[gnu::visibility("default")]] void \
    zygisk_companion_entry(int fd) { func(fd); }
