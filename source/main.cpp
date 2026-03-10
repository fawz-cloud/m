#include "zygisk.hpp"
#include "config.h"
#include "hook.h"

#include <string>
#include <android/log.h>

#ifdef NDEBUG
  #define LOGI(...) ((void)0)
  #define LOGE(...) ((void)0)
#else
  #define LOG_TAG "SurfaceComposer"
  #define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
  #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

class SpooferModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api_ = api;
        this->env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // ONLY extract the package name here (lightweight)
        // Config load + hooks are deferred to postAppSpecialize
        const char *raw_name = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (!raw_name) {
            api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        package_name_ = std::string(raw_name);
        env_->ReleaseStringUTFChars(args->nice_name, raw_name);

        // Quick check: load config to determine if this app is a target
        // (file I/O in pre-specialize is acceptable for small JSON)
        config_ = load_config();

        if (!config_.enabled || !is_target_app(config_, package_name_)) {
            api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            should_hook_ = false;
            return;
        }

        LOGI("Target: %s", package_name_.c_str());
        should_hook_ = true;
        // Do NOT call DLCLOSE — we need the library to stay loaded
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        (void)args;
        if (!should_hook_) return;

        // All hook installation happens here, AFTER the process is forked
        // and specialized. This is the correct lifecycle stage:
        // - The process is isolated (no risk of contaminating Zygote)
        // - SELinux context is set (correct file access permissions)
        // - Other libraries are loaded (hook targets are available)
        install_hooks(config_);
        install_jni_hooks(env_, config_);

        LOGI("Active: %s", package_name_.c_str());
    }

private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    std::string package_name_;
    SpoofConfig config_;
    bool should_hook_ = false;
};

REGISTER_ZYGISK_MODULE(SpooferModule)
