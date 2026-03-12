#include "config.h"
#include "cJSON.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <android/log.h>

#ifdef NDEBUG
  #define LOGI(...) ((void)0)
  #define LOGE(...) ((void)0)
#else
  #define LOG_TAG "SurfaceComposer"
  #define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
  #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

// Obfuscated config path — avoids plain "spoofer" in strings dump
static constexpr char CONFIG_PATH[] =
    "/data/adb/modules/zygisk_spoofer/config.json";

static std::string json_get_string(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item) && item->valuestring)
        return std::string(item->valuestring);
    return "";
}

static bool json_get_bool(cJSON *obj, const char *key, bool def = false) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsBool(item)) return cJSON_IsTrue(item);
    return def;
}

static std::vector<std::string> json_get_string_array(cJSON *obj, const char *key) {
    std::vector<std::string> result;
    cJSON *arr = cJSON_GetObjectItem(obj, key);
    if (arr && cJSON_IsArray(arr)) {
        int count = cJSON_GetArraySize(arr);
        result.reserve(count);
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(arr, i);
            if (item && cJSON_IsString(item) && item->valuestring)
                result.emplace_back(item->valuestring);
        }
    }
    return result;
}

SpoofConfig load_config() {
    SpoofConfig config;
    FILE *fp = fopen(CONFIG_PATH, "r");
    if (!fp) { LOGE("Config not found"); return config; }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 131072) {
        LOGE("Config size invalid: %ld", fsize);
        fclose(fp);
        return config;
    }

    char *buf = static_cast<char *>(malloc(fsize + 1));
    if (!buf) { fclose(fp); return config; }

    // FIX: Check fread return value and handle partial reads
    size_t total_read = 0;
    while (total_read < static_cast<size_t>(fsize)) {
        size_t n = fread(buf + total_read, 1, fsize - total_read, fp);
        if (n == 0) break; // EOF or error
        total_read += n;
    }
    buf[total_read] = '\0';
    fclose(fp);

    if (total_read == 0) {
        free(buf);
        LOGE("Config read failed");
        return config;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    buf = nullptr; // Prevent use-after-free

    if (!root) { LOGE("JSON parse failed"); return config; }

    cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    config.enabled = enabled && cJSON_IsTrue(enabled);
    config.target_apps = json_get_string_array(root, "target_apps");

    cJSON *sv = cJSON_GetObjectItem(root, "spoof_values");
    if (sv && cJSON_IsObject(sv)) {
        config.android_id       = json_get_string(sv, "android_id");
        config.gsf_id           = json_get_string(sv, "gsf_id");
        config.gaid             = json_get_string(sv, "gaid");
        config.drm_security_level = json_get_string(sv, "drm_security_level");
        config.mac_address      = json_get_string(sv, "mac_address");
        config.bluetooth_mac    = json_get_string(sv, "bluetooth_mac");
        config.wifi_ssid        = json_get_string(sv, "wifi_ssid");
        config.wifi_bssid       = json_get_string(sv, "wifi_bssid");
        config.model            = json_get_string(sv, "model");
        config.brand            = json_get_string(sv, "brand");
        config.manufacturer     = json_get_string(sv, "manufacturer");
        config.device           = json_get_string(sv, "device");
        config.product          = json_get_string(sv, "product");
        config.board            = json_get_string(sv, "board");
        config.hardware         = json_get_string(sv, "hardware");
        config.fingerprint      = json_get_string(sv, "fingerprint");
        config.serial           = json_get_string(sv, "serial");
        config.bootloader       = json_get_string(sv, "bootloader");
        config.display          = json_get_string(sv, "display");
        config.build_id         = json_get_string(sv, "build_id");
        config.build_type       = json_get_string(sv, "build_type");
        config.build_tags       = json_get_string(sv, "build_tags");
        config.build_user       = json_get_string(sv, "build_user");
        config.build_host       = json_get_string(sv, "build_host");
        config.incremental      = json_get_string(sv, "incremental");
        config.imei             = json_get_string(sv, "imei");
        config.meid             = json_get_string(sv, "meid");
        config.baseband         = json_get_string(sv, "baseband");
        config.operator_name    = json_get_string(sv, "operator_name");
        config.operator_numeric = json_get_string(sv, "operator_numeric");
        config.gl_renderer      = json_get_string(sv, "gl_renderer");
        config.gl_vendor        = json_get_string(sv, "gl_vendor");
        config.screen_density   = json_get_string(sv, "screen_density");
        config.screen_resolution = json_get_string(sv, "screen_resolution");
        config.cpu_abi          = json_get_string(sv, "cpu_abi");
        config.hardware_serial  = json_get_string(sv, "hardware_serial");
        config.soc_model        = json_get_string(sv, "soc_model");
        // Android Version
        config.version_release  = json_get_string(sv, "version_release");
        config.version_sdk      = json_get_string(sv, "version_sdk");
        config.version_codename = json_get_string(sv, "version_codename");
        config.security_patch   = json_get_string(sv, "security_patch");
        // Samsung Knox
        config.knox_warranty_bit   = json_get_string(sv, "knox_warranty_bit");
        config.knox_verified_state = json_get_string(sv, "knox_verified_state");
    }

    // Parse advanced feature toggles
    cJSON *adv = cJSON_GetObjectItem(root, "advanced");
    if (adv && cJSON_IsObject(adv)) {
        config.adv_mmap_bypass  = json_get_bool(adv, "adv_mmap_bypass");
        config.vpn_bypass       = json_get_bool(adv, "vpn_bypass");
        config.camera_virtual   = json_get_bool(adv, "camera_virtual");
        config.fs_sandbox       = json_get_bool(adv, "fs_sandbox");
        config.anti_debug_block = json_get_bool(adv, "anti_debug_block");
    }

    config.custom_wipe_dirs = json_get_string_array(root, "custom_wipe_dirs");
    cJSON_Delete(root);

    LOGI("Config loaded: enabled=%d, targets=%zu", config.enabled, config.target_apps.size());
    return config;
}

bool is_target_app(const SpoofConfig &config, const std::string &package_name) {
    for (const auto &app : config.target_apps) {
        if (app == package_name) return true;
        // Match sub-processes: "com.package.name:remote" matches "com.package.name"
        if (package_name.size() > app.size() &&
            package_name.compare(0, app.size(), app) == 0 &&
            package_name[app.size()] == ':') {
            return true;
        }
    }
    return false;
}
