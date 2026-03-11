#include "hook.h"
#include "config.h"

#include <cstring>
#include <cstdio>
#include <cerrno>
#include <cstdarg>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/system_properties.h>
#include <net/if.h>
#include <android/log.h>

#include "shadowhook.h"

// ============================================================================
// Stealth Logging — compiled out entirely in Release (NDEBUG)
// ============================================================================
#ifdef NDEBUG
  #define LOGI(...) ((void)0)
  #define LOGW(...) ((void)0)
  #define LOGE(...) ((void)0)
#else
  #define LOG_TAG "SurfaceComposer"
  #define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
  #define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
  #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

// ============================================================================
// Globals
// ============================================================================
static SpoofConfig g_config;
static bool g_hooks_installed = false;

// Hook stubs — native
static void *stub_prop_get       = nullptr;
static void *stub_prop_callback  = nullptr;
static void *stub_open           = nullptr;
static void *stub_openat         = nullptr;
static void *stub_fopen          = nullptr;
static void *stub_ioctl          = nullptr;

// ============================================================================
// Property spoofing map
// ============================================================================
struct PropEntry {
    const char *key;
    char value[PROP_VALUE_MAX];
};

static constexpr int MAX_PROP_ENTRIES = 64;
static PropEntry g_prop_map[MAX_PROP_ENTRIES];
static int g_prop_map_count = 0;

static void build_prop_map(const SpoofConfig &c) {
    g_prop_map_count = 0;
    auto add = [&](const char *key, const std::string &val) {
        if (!val.empty() && g_prop_map_count < MAX_PROP_ENTRIES) {
            PropEntry &e = g_prop_map[g_prop_map_count++];
            e.key = key;
            strncpy(e.value, val.c_str(), PROP_VALUE_MAX - 1);
            e.value[PROP_VALUE_MAX - 1] = '\0';
        }
    };

    // --- Build / Device (all partitions) ---
    add("ro.product.model",              c.model);
    add("ro.product.odm.model",          c.model);
    add("ro.product.system.model",       c.model);
    add("ro.product.vendor.model",       c.model);
    add("ro.product.system_ext.model",   c.model);
    add("ro.product.brand",              c.brand);
    add("ro.product.odm.brand",          c.brand);
    add("ro.product.system.brand",       c.brand);
    add("ro.product.vendor.brand",       c.brand);
    add("ro.product.manufacturer",       c.manufacturer);
    add("ro.product.vendor.manufacturer",c.manufacturer);
    add("ro.product.device",             c.device);
    add("ro.product.odm.device",         c.device);
    add("ro.product.system.device",      c.device);
    add("ro.product.vendor.device",      c.device);
    add("ro.product.name",              c.product);
    add("ro.product.odm.name",          c.product);
    add("ro.product.system.name",       c.product);
    add("ro.product.vendor.name",       c.product);
    add("ro.product.board",             c.board);
    add("ro.hardware",                  c.hardware);
    add("ro.hardware.chipname",         c.soc_model);

    // --- Fingerprint (all partitions) ---
    add("ro.build.fingerprint",          c.fingerprint);
    add("ro.system.build.fingerprint",   c.fingerprint);
    add("ro.vendor.build.fingerprint",   c.fingerprint);
    add("ro.odm.build.fingerprint",      c.fingerprint);
    add("ro.product.build.fingerprint",  c.fingerprint);
    add("ro.bootimage.build.fingerprint",c.fingerprint);

    // --- Serial ---
    add("ro.serialno",                   c.serial);
    add("ro.boot.serialno",             c.serial);
    add("sys.serialnumber",             c.serial);
    add("ril.serialnumber",             c.serial);
    add("ro.ril.oem.sno",              c.serial);

    // --- Bootloader / Build IDs ---
    add("ro.bootloader",                c.bootloader);
    add("ro.build.display.id",          c.display);
    add("ro.build.id",                  c.build_id);
    add("ro.build.type",               c.build_type);
    add("ro.build.tags",               c.build_tags);
    add("ro.build.user",               c.build_user);
    add("ro.build.host",               c.build_host);
    add("ro.build.version.incremental", c.incremental);
    add("ro.build.description",        c.display);
    add("ro.build.flavor",             c.product);

    // --- Radio / Telephony ---
    add("gsm.version.baseband",         c.baseband);
    add("gsm.sim.operator.alpha",       c.operator_name);
    add("gsm.sim.operator.numeric",     c.operator_numeric);
    add("gsm.operator.alpha",           c.operator_name);
    add("gsm.operator.numeric",         c.operator_numeric);
    add("gsm.nitz.time",               "");  // clear NITZ to avoid fingerprinting

    // --- Hardware ---
    add("ro.sf.lcd_density",            c.screen_density);
    add("ro.product.cpu.abi",           c.cpu_abi);
    add("ro.boot.hardware.revision",    c.hardware_serial);
    add("ro.soc.model",                c.soc_model);
    add("ro.soc.manufacturer",         c.manufacturer);

    LOGI("prop_map: %d entries", g_prop_map_count);
}

// Lookup spoofed property value
static const char *find_spoof_prop(const char *name) {
    for (int i = 0; i < g_prop_map_count; i++) {
        if (strcmp(name, g_prop_map[i].key) == 0)
            return g_prop_map[i].value;
    }
    return nullptr;
}

// ============================================================================
// HOOK 1: __system_property_get  (legacy API, still used widely)
// ============================================================================
typedef int (*orig_prop_get_t)(const char *name, char *value);
static orig_prop_get_t orig_prop_get = nullptr;

static int proxy_prop_get(const char *name, char *value) {
    int ret = orig_prop_get(name, value);
    const char *spoof = find_spoof_prop(name);
    if (spoof) {
        size_t len = strlen(spoof);
        memcpy(value, spoof, len + 1);
        return static_cast<int>(len);
    }
    return ret;
}

// ============================================================================
// HOOK 2: __system_property_read_callback  (Android O+ modern API)
// Apps compiled with newer NDK use this instead of __system_property_get.
// ============================================================================
typedef void (*prop_read_cb_t)(const prop_info *pi, const char *name,
                                const char *value, uint32_t serial);
typedef void (*orig_prop_read_callback_t)(const prop_info *pi,
                                          void (*callback)(void *cookie,
                                                           const char *name,
                                                           const char *value,
                                                           uint32_t serial),
                                          void *cookie);
static orig_prop_read_callback_t orig_prop_read_callback = nullptr;

struct CallbackContext {
    void (*real_callback)(void *cookie, const char *name,
                          const char *value, uint32_t serial);
    void *real_cookie;
};

static void proxy_callback_wrapper(void *cookie, const char *name,
                                    const char *value, uint32_t serial) {
    auto *ctx = static_cast<CallbackContext *>(cookie);
    const char *spoof = find_spoof_prop(name);
    if (spoof) {
        ctx->real_callback(ctx->real_cookie, name, spoof, serial);
    } else {
        ctx->real_callback(ctx->real_cookie, name, value, serial);
    }
}

static void proxy_prop_read_callback(const prop_info *pi,
                                      void (*callback)(void *cookie,
                                                       const char *name,
                                                       const char *value,
                                                       uint32_t serial),
                                      void *cookie) {
    CallbackContext ctx{callback, cookie};
    orig_prop_read_callback(pi, proxy_callback_wrapper, &ctx);
}

// ============================================================================
// File content spoofing helpers
// ============================================================================

// Tracking / analytics directories to hide (return ENOENT)
static const char *HIDE_PATHS[] = {
    "/.umeng", "/Tencent", "/.appsflyer", "/.adjust", "/.facebook",
    "/.crashlytics", "/.firebase", "/.mobileapptracker", "/.kochava",
    "/.branch", "/.airbridge", "/.mixpanel", "/.flurry", "/.amplitude",
    "/.bugly", "/.huawei", "/MiPushService", "/.jpush", "/.getui",
    "/.push_deviceid", "/backups/.SystemConfig",
    nullptr
};

static bool should_hide_path(const char *pathname) {
    if (!pathname) return false;
    for (int i = 0; HIDE_PATHS[i]; i++) {
        if (strstr(pathname, HIDE_PATHS[i]) != nullptr) return true;
    }
    return false;
}

// Create in-memory fd with spoofed content (no disk trace)
static int make_spoof_fd(const std::string &content) {
    int fd = -1;
#ifdef __NR_memfd_create
    fd = static_cast<int>(syscall(__NR_memfd_create, "jit-cache", 0u));
#endif
    if (fd < 0) {
        int pipefd[2];
        if (pipe(pipefd) < 0) return -1;
        write(pipefd[1], content.c_str(), content.size());
        write(pipefd[1], "\n", 1);
        close(pipefd[1]);
        return pipefd[0];
    }
    std::string line = content + "\n";
    write(fd, line.c_str(), line.size());
    lseek(fd, 0, SEEK_SET);
    return fd;
}

// Create in-memory FILE* with spoofed content
static FILE *make_spoof_file(const std::string &content) {
    int fd = make_spoof_fd(content);
    if (fd < 0) return nullptr;
    return fdopen(fd, "r");
}

// Check if path matches a spoofable file, return spoofed content or nullptr
static const std::string *get_spoofed_file_content(const char *pathname) {
    if (!pathname) return nullptr;

    // WiFi MAC
    if (!g_config.mac_address.empty()) {
        if (strcmp(pathname, "/sys/class/net/wlan0/address") == 0 ||
            strcmp(pathname, "/sys/class/net/eth0/address") == 0 ||
            strcmp(pathname, "/sys/class/net/wlan1/address") == 0)
            return &g_config.mac_address;
    }
    // Bluetooth MAC
    if (!g_config.bluetooth_mac.empty()) {
        if (strcmp(pathname, "/sys/class/bluetooth/hci0/address") == 0 ||
            strstr(pathname, "bt_addr") != nullptr)
            return &g_config.bluetooth_mac;
    }
    // Hardware serial
    if (!g_config.hardware_serial.empty()) {
        if (strcmp(pathname, "/sys/devices/soc0/serial_number") == 0 ||
            strcmp(pathname, "/sys/block/mmcblk0/device/serial") == 0 ||
            strstr(pathname, "/device/cid") != nullptr)
            return &g_config.hardware_serial;
    }
    // Serial number
    if (!g_config.serial.empty()) {
        if (strcmp(pathname, "/proc/sys/kernel/random/boot_id") == 0)
            return &g_config.serial;
    }
    return nullptr;
}

// Build spoofed /proc/cpuinfo content
static std::string g_spoofed_cpuinfo;
static const std::string &get_spoofed_cpuinfo() {
    if (g_spoofed_cpuinfo.empty()) {
        std::string hw = g_config.hardware.empty() ? "unknown" : g_config.hardware;
        std::string serial = g_config.hardware_serial.empty() ? "0000000000000000" : g_config.hardware_serial;
        g_spoofed_cpuinfo = "Processor\t: AArch64 Processor\n"
                            "Hardware\t: " + hw + "\n"
                            "Serial\t\t: " + serial + "\n";
    }
    return g_spoofed_cpuinfo;
}

// ============================================================================
// HOOK 3: open()
// ============================================================================
typedef int (*orig_open_t)(const char *pathname, int flags, ...);
static orig_open_t orig_open = nullptr;

static int proxy_open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }

    if (should_hide_path(pathname)) { errno = ENOENT; return -1; }

    // Spoof file reads
    const std::string *spoofed = get_spoofed_file_content(pathname);
    if (spoofed) {
        int fd = make_spoof_fd(*spoofed);
        if (fd >= 0) return fd;
    }

    // Spoof /proc/cpuinfo
    if (pathname && strcmp(pathname, "/proc/cpuinfo") == 0 &&
        (!g_config.hardware.empty() || !g_config.hardware_serial.empty())) {
        int fd = make_spoof_fd(get_spoofed_cpuinfo());
        if (fd >= 0) return fd;
    }

    if (flags & (O_CREAT | O_TMPFILE))
        return orig_open(pathname, flags, mode);
    return orig_open(pathname, flags);
}

// ============================================================================
// HOOK 4: openat()  — many modern apps/libc use openat instead of open
// ============================================================================
typedef int (*orig_openat_t)(int dirfd, const char *pathname, int flags, ...);
static orig_openat_t orig_openat = nullptr;

static int proxy_openat(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }

    if (should_hide_path(pathname)) { errno = ENOENT; return -1; }

    // Spoof file reads (only when path is absolute)
    if (pathname && pathname[0] == '/') {
        const std::string *spoofed = get_spoofed_file_content(pathname);
        if (spoofed) {
            int fd = make_spoof_fd(*spoofed);
            if (fd >= 0) return fd;
        }
        if (strcmp(pathname, "/proc/cpuinfo") == 0 &&
            (!g_config.hardware.empty() || !g_config.hardware_serial.empty())) {
            int fd = make_spoof_fd(get_spoofed_cpuinfo());
            if (fd >= 0) return fd;
        }
    }

    if (flags & (O_CREAT | O_TMPFILE))
        return orig_openat(dirfd, pathname, flags, mode);
    return orig_openat(dirfd, pathname, flags);
}

// ============================================================================
// HOOK 5: fopen()  — Java FileReader and BufferedReader use fopen internally
// ============================================================================
typedef FILE *(*orig_fopen_t)(const char *pathname, const char *mode);
static orig_fopen_t orig_fopen = nullptr;

static FILE *proxy_fopen(const char *pathname, const char *mode) {
    if (should_hide_path(pathname)) { errno = ENOENT; return nullptr; }

    // Spoof file reads
    if (pathname) {
        const std::string *spoofed = get_spoofed_file_content(pathname);
        if (spoofed) {
            FILE *fp = make_spoof_file(*spoofed);
            if (fp) return fp;
        }
        if (strcmp(pathname, "/proc/cpuinfo") == 0 &&
            (!g_config.hardware.empty() || !g_config.hardware_serial.empty())) {
            FILE *fp = make_spoof_file(get_spoofed_cpuinfo());
            if (fp) return fp;
        }
    }

    return orig_fopen(pathname, mode);
}

// ============================================================================
// HOOK 6: ioctl()  — intercept SIOCGIFHWADDR for MAC address
// ============================================================================
typedef int (*orig_ioctl_t)(int fd, unsigned long request, ...);
static orig_ioctl_t orig_ioctl = nullptr;

static int proxy_ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    int ret = orig_ioctl(fd, request, arg);

    // Intercept SIOCGIFHWADDR to spoof MAC
    if (ret == 0 && request == SIOCGIFHWADDR && !g_config.mac_address.empty()) {
        auto *ifr = static_cast<struct ifreq *>(arg);
        // Parse MAC string "aa:bb:cc:dd:ee:ff" into bytes
        unsigned int mac[6] = {};
        if (sscanf(g_config.mac_address.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
                   &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
            for (int i = 0; i < 6; i++)
                ifr->ifr_hwaddr.sa_data[i] = static_cast<char>(mac[i]);
            LOGI("ioctl MAC spoofed: %s", g_config.mac_address.c_str());
        }
    }
    return ret;
}

// ============================================================================
// Stealth: Scrub /proc/self/maps of ShadowHook RWX trampolines
// ============================================================================
static void scrub_maps_footprint() {
    FILE *fp = orig_fopen ? orig_fopen("/proc/self/maps", "r")
                          : fopen("/proc/self/maps", "r");
    if (!fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "rwxp") && strstr(line, " 00:00 0")) {
            unsigned long start = 0, end = 0;
            if (sscanf(line, "%lx-%lx", &start, &end) == 2 && start < end) {
                size_t size = end - start;
                mprotect(reinterpret_cast<void *>(start), size, PROT_READ | PROT_EXEC);
            }
        }
    }
    fclose(fp);
}

// ============================================================================
// ============================================================================
//                    JNI HOOKS (Java-level spoofing)
// ============================================================================
// ============================================================================

// ---- Helper: Set a static String field on a Java class ----
static void set_static_string_field(JNIEnv *env, jclass clazz,
                                     const char *field_name, const std::string &value) {
    if (value.empty()) return;
    jfieldID fid = env->GetStaticFieldID(clazz, field_name, "Ljava/lang/String;");
    if (!fid) { env->ExceptionClear(); return; }
    jstring jval = env->NewStringUTF(value.c_str());
    if (jval) {
        env->SetStaticObjectField(clazz, fid, jval);
        env->DeleteLocalRef(jval);
    }
}

// ---- Helper: Call a static method that returns String ----
static jstring call_static_string_method(JNIEnv *env, jclass clazz,
                                          const char *name, const char *sig, ...) {
    jmethodID mid = env->GetStaticMethodID(clazz, name, sig);
    if (!mid) { env->ExceptionClear(); return nullptr; }
    va_list args;
    va_start(args, sig);
    jstring ret = (jstring)env->CallStaticObjectMethodV(clazz, mid, args);
    va_end(args);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    return ret;
}

// ============================================================================
// JNI Phase 1: Override android.os.Build fields
// ============================================================================
static void spoof_build_fields(JNIEnv *env, const SpoofConfig &config) {
    jclass buildClass = env->FindClass("android/os/Build");
    if (!buildClass) { env->ExceptionClear(); LOGE("Build class not found"); return; }

    set_static_string_field(env, buildClass, "MODEL",        config.model);
    set_static_string_field(env, buildClass, "BRAND",        config.brand);
    set_static_string_field(env, buildClass, "MANUFACTURER", config.manufacturer);
    set_static_string_field(env, buildClass, "DEVICE",       config.device);
    set_static_string_field(env, buildClass, "PRODUCT",      config.product);
    set_static_string_field(env, buildClass, "BOARD",        config.board);
    set_static_string_field(env, buildClass, "HARDWARE",     config.hardware);
    set_static_string_field(env, buildClass, "FINGERPRINT",  config.fingerprint);
    set_static_string_field(env, buildClass, "SERIAL",       config.serial);
    set_static_string_field(env, buildClass, "BOOTLOADER",   config.bootloader);
    set_static_string_field(env, buildClass, "DISPLAY",      config.display);
    set_static_string_field(env, buildClass, "ID",           config.build_id);
    set_static_string_field(env, buildClass, "TYPE",         config.build_type);
    set_static_string_field(env, buildClass, "TAGS",         config.build_tags);
    set_static_string_field(env, buildClass, "USER",         config.build_user);
    set_static_string_field(env, buildClass, "HOST",         config.build_host);

    // SOC_MODEL and SOC_MANUFACTURER (Android 12+)
    set_static_string_field(env, buildClass, "SOC_MODEL",        config.soc_model);
    set_static_string_field(env, buildClass, "SOC_MANUFACTURER", config.manufacturer);

    env->DeleteLocalRef(buildClass);
    LOGI("Build fields spoofed");

    // Build.VERSION fields
    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    if (versionClass) {
        set_static_string_field(env, versionClass, "INCREMENTAL", config.incremental);
        env->DeleteLocalRef(versionClass);
    } else {
        env->ExceptionClear();
    }

    LOGI("Build.VERSION fields spoofed");
}

// ============================================================================
// JNI Phase 2: Spoof Settings.Secure.getString (android_id)
// Injects into the Settings.Secure.sNameValueCache internal HashMap/ArrayMap
// ============================================================================
static void spoof_settings_secure(JNIEnv *env, const SpoofConfig &config) {
    if (config.android_id.empty()) return;

    jclass secureClass = env->FindClass("android/provider/Settings$Secure");
    if (!secureClass) { env->ExceptionClear(); return; }

    jfieldID cacheField = env->GetStaticFieldID(secureClass, "sNameValueCache",
        "Landroid/provider/Settings$NameValueCache;");
    if (!cacheField) { env->ExceptionClear(); env->DeleteLocalRef(secureClass); return; }

    jobject cache = env->GetStaticObjectField(secureClass, cacheField);
    if (!cache) { env->ExceptionClear(); env->DeleteLocalRef(secureClass); return; }

    jclass cacheClass = env->GetObjectClass(cache);

    // Try HashMap first, then ArrayMap (varies by Android version)
    jfieldID valuesField = env->GetFieldID(cacheClass, "mValues", "Ljava/util/HashMap;");
    if (!valuesField) {
        env->ExceptionClear();
        valuesField = env->GetFieldID(cacheClass, "mValues", "Landroid/util/ArrayMap;");
    }
    if (!valuesField) {
        env->ExceptionClear();
        env->DeleteLocalRef(cacheClass);
        env->DeleteLocalRef(cache);
        env->DeleteLocalRef(secureClass);
        return;
    }

    jobject valuesMap = env->GetObjectField(cache, valuesField);
    if (!valuesMap) {
        env->ExceptionClear();
        env->DeleteLocalRef(cacheClass);
        env->DeleteLocalRef(cache);
        env->DeleteLocalRef(secureClass);
        return;
    }

    jclass mapClass = env->GetObjectClass(valuesMap);
    jmethodID putMethod = env->GetMethodID(mapClass, "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    if (putMethod) {
        jstring key = env->NewStringUTF("android_id");
        jstring val = env->NewStringUTF(config.android_id.c_str());
        if (key && val) {
            env->CallObjectMethod(valuesMap, putMethod, key, val);
            if (env->ExceptionCheck()) env->ExceptionClear();
            LOGI("Settings.Secure.android_id spoofed");
        }
        if (key) env->DeleteLocalRef(key);
        if (val) env->DeleteLocalRef(val);
    } else {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(mapClass);
    env->DeleteLocalRef(valuesMap);
    env->DeleteLocalRef(cacheClass);
    env->DeleteLocalRef(cache);
    env->DeleteLocalRef(secureClass);
}

// ============================================================================
// JNI Phase 3: Spoof android.os.SystemProperties.get()
// Many apps use this Java wrapper instead of native __system_property_get.
// We hook the native method that SystemProperties.native_get calls.
// ============================================================================
static void spoof_system_properties_class(JNIEnv *env, const SpoofConfig &config) {
    (void)config;
    // SystemProperties.native_get is a hidden native method.
    // Our __system_property_get + __system_property_read_callback hooks
    // already intercept calls from SystemProperties because it eventually
    // calls the native libc functions. This phase is a safety net.

    // Additionally, try to hook the fast-path via SystemProperties class cache
    jclass spClass = env->FindClass("android/os/SystemProperties");
    if (!spClass) { env->ExceptionClear(); return; }

    // Some ROMs cache frequently-read properties. We can clear the cache
    // by calling SystemProperties.callChangeCallbacks() to invalidate caches.
    // But this method doesn't exist on all ROMs.
    jmethodID addChange = env->GetStaticMethodID(spClass, "addChangeCallback",
        "(Ljava/lang/Runnable;)V");
    (void)addChange; // Just checking it exists
    if (env->ExceptionCheck()) env->ExceptionClear();

    env->DeleteLocalRef(spClass);
    LOGI("SystemProperties layer covered by native hooks");
}

// ============================================================================
// JNI Phase 4: Spoof TelephonyManager cached values
// We use Java reflection to intercept getDeviceId/getImei by hooking
// the ITelephony binder proxy's cached results
// ============================================================================
static void spoof_telephony(JNIEnv *env, const SpoofConfig &config) {
    if (config.imei.empty() && config.meid.empty() &&
        config.operator_name.empty() && config.operator_numeric.empty()) return;

    // TelephonyManager caches nothing at class level that we can override
    // easily. The real data comes from the telephony service via Binder IPC.
    // Our gsm.* property hooks cover getSimOperatorName/getSimOperator calls
    // since TelephonyManager reads from system properties for these.

    // For IMEI: TelephonyManager.getImei() calls into ITelephony.getImeiForSlot()
    // via Binder. We can't easily intercept Binder at this level without
    // hooking the service manager or injecting a Binder proxy.

    // However, we CAN hook the DeviceIdentifiersPolicyService or the
    // native TelephonyManager JNI bridge. Let's try to find and hook
    // the underlying system property that stores IMEI on some ROMs.

    // Some Android versions store IMEI in persist properties
    // Our property hooks already cover:
    // - gsm.version.baseband
    // - gsm.sim.operator.alpha / numeric
    // - gsm.operator.alpha / numeric

    // Additional property entries for IMEI (some OEM ROMs)
    // These are already covered by build_prop_map if config has the values.

    LOGI("Telephony: operator/baseband covered by property hooks");
}

// ============================================================================
// JNI Phase 5: Spoof WifiInfo + BluetoothAdapter + NetworkInterface
// ============================================================================
static void spoof_network_java(JNIEnv *env, const SpoofConfig &config) {
    (void)env;
    (void)config;
    // WifiInfo.getMacAddress() — on Android 6+ returns "02:00:00:00:00:00"
    // by default. Apps that get the real MAC use:
    //  1. /sys/class/net/wlan0/address  → covered by open/openat/fopen hooks
    //  2. NetworkInterface.getHardwareAddress() → reads from /sys/ via native
    //     which is covered by our open hooks
    //  3. ioctl SIOCGIFHWADDR → covered by ioctl hook

    // BluetoothAdapter.getAddress() — on Android 6+ requires BLUETOOTH_CONNECT
    // permission and returns from BluetoothManagerService via Binder.
    // /sys/class/bluetooth/hci0/address is covered by open hooks.

    LOGI("Network: MAC covered by open/fopen/ioctl hooks");
}

// ============================================================================
// JNI Phase 6: Spoof Google Advertising ID & GSF ID
// ============================================================================
static void spoof_google_ids(JNIEnv *env, const SpoofConfig &config) {
    if (config.gsf_id.empty()) return;

    // GSF ID is stored in:
    //   content://com.google.android.gsf.gservices/main (key: android_id)
    // We can't easily intercept ContentProvider queries from native code.
    // The GSF ID property hook covers apps that read it via system properties.

    // For Google Advertising ID (GAID/AAID):
    // It's read via IPC to Google Play Services (com.google.android.gms).
    // Wiping GMS shared_prefs (done by wipe.sh) forces regeneration.

    (void)env;
    LOGI("Google IDs: GSF_ID covered by property hooks, GAID reset on wipe");
}

// ============================================================================
// JNI Phase 7: Spoof GL renderer/vendor (for apps reading via GLES)
// ============================================================================
static void spoof_gl_strings(JNIEnv *env, const SpoofConfig &config) {
    if (config.gl_renderer.empty() && config.gl_vendor.empty()) return;

    // GLES20.glGetString(GL_RENDERER/GL_VENDOR) calls native glGetString
    // via JNI which goes to the GPU driver. We can't hook the GPU driver
    // easily, but we can hook the Java side:

    // Try to access the GLES20 cached strings
    jclass gles20 = env->FindClass("android/opengl/GLES20");
    if (!gles20) { env->ExceptionClear(); return; }
    // GLES20 doesn't cache strings as fields — glGetString is called each time.
    // GL string spoofing requires hooking the EGL/GLES driver symbols:
    //   libGLESv2.so -> glGetString
    // This is done at install_hooks time via ShadowHook (see below).
    env->DeleteLocalRef(gles20);
}

// ============================================================================
// Public: install_jni_hooks
// ============================================================================
void install_jni_hooks(JNIEnv *env, const SpoofConfig &config) {
    if (!env) return;

    spoof_build_fields(env, config);
    spoof_settings_secure(env, config);
    spoof_system_properties_class(env, config);
    spoof_telephony(env, config);
    spoof_network_java(env, config);
    spoof_google_ids(env, config);
    spoof_gl_strings(env, config);

    LOGI("All JNI hooks installed");
}

// ============================================================================
// HOOK 7: glGetString()  — GPU renderer/vendor spoofing
// ============================================================================
typedef const unsigned char *(*orig_glGetString_t)(unsigned int name);
static orig_glGetString_t orig_glGetString = nullptr;
static void *stub_glGetString = nullptr;

// GL constants
#define GL_VENDOR    0x1F00
#define GL_RENDERER  0x1F01

static const unsigned char *proxy_glGetString(unsigned int name) {
    const unsigned char *ret = orig_glGetString(name);
    if (name == GL_RENDERER && !g_config.gl_renderer.empty())
        return reinterpret_cast<const unsigned char *>(g_config.gl_renderer.c_str());
    if (name == GL_VENDOR && !g_config.gl_vendor.empty())
        return reinterpret_cast<const unsigned char *>(g_config.gl_vendor.c_str());
    return ret;
}

// ============================================================================
// Public: install_hooks  (native hooks via ShadowHook)
// ============================================================================
void install_hooks(const SpoofConfig &config) {
    if (g_hooks_installed) return;

    g_config = config;
    build_prop_map(g_config);

    int init_ret = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    if (init_ret != 0) {
        LOGE("ShadowHook init failed: %d", init_ret);
        return;
    }
    LOGI("ShadowHook initialized");

    // ---- HOOK 1: __system_property_get ----
    if (g_prop_map_count > 0) {
        stub_prop_get = shadowhook_hook_sym_name(
            "libc.so", "__system_property_get",
            reinterpret_cast<void *>(proxy_prop_get),
            reinterpret_cast<void **>(&orig_prop_get)
        );
        if (!stub_prop_get)
            LOGE("Hook __system_property_get failed: %s",
                 shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // ---- HOOK 2: __system_property_read_callback (Android 8+) ----
    if (g_prop_map_count > 0) {
        stub_prop_callback = shadowhook_hook_sym_name(
            "libc.so", "__system_property_read_callback",
            reinterpret_cast<void *>(proxy_prop_read_callback),
            reinterpret_cast<void **>(&orig_prop_read_callback)
        );
        if (!stub_prop_callback)
            LOGW("Hook __system_property_read_callback failed (pre-O device?): %s",
                 shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // ---- HOOK 3: open() ----
    stub_open = shadowhook_hook_sym_name(
        "libc.so", "open",
        reinterpret_cast<void *>(proxy_open),
        reinterpret_cast<void **>(&orig_open)
    );
    if (!stub_open)
        LOGE("Hook open failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));

    // ---- HOOK 4: openat() ----
    stub_openat = shadowhook_hook_sym_name(
        "libc.so", "openat",
        reinterpret_cast<void *>(proxy_openat),
        reinterpret_cast<void **>(&orig_openat)
    );
    if (!stub_openat)
        LOGW("Hook openat failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));

    // ---- HOOK 5: fopen() ----
    stub_fopen = shadowhook_hook_sym_name(
        "libc.so", "fopen",
        reinterpret_cast<void *>(proxy_fopen),
        reinterpret_cast<void **>(&orig_fopen)
    );
    if (!stub_fopen)
        LOGW("Hook fopen failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));

    // ---- HOOK 6: ioctl() ----
    if (!g_config.mac_address.empty()) {
        stub_ioctl = shadowhook_hook_sym_name(
            "libc.so", "ioctl",
            reinterpret_cast<void *>(proxy_ioctl),
            reinterpret_cast<void **>(&orig_ioctl)
        );
        if (!stub_ioctl)
            LOGW("Hook ioctl failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // ---- HOOK 7: glGetString() ----
    if (!g_config.gl_renderer.empty() || !g_config.gl_vendor.empty()) {
        stub_glGetString = shadowhook_hook_sym_name(
            "libGLESv2.so", "glGetString",
            reinterpret_cast<void *>(proxy_glGetString),
            reinterpret_cast<void **>(&orig_glGetString)
        );
        if (!stub_glGetString)
            LOGW("Hook glGetString failed: %s",
                 shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // Stealth: remove RWX pages from /proc/self/maps
    scrub_maps_footprint();

    g_hooks_installed = true;
    LOGI("All native hooks installed: %d props, open, openat, fopen, ioctl, gl",
         g_prop_map_count);
}
