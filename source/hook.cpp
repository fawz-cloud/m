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
#include <sys/system_properties.h>
#include <android/log.h>

#include "shadowhook.h"

// ============================================================================
// Stealth Logging — compiled out entirely in Release (NDEBUG)
// Use a generic tag that blends in with Android system logs
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
static void *prop_hook_stub = nullptr;
static void *open_hook_stub = nullptr;

// ============================================================================
// Property spoofing map
// Stores copied strings to avoid dangling pointers when g_config is reassigned.
// ============================================================================
struct PropEntry {
    const char *key;          // compile-time string literal, always valid
    char value[PROP_VALUE_MAX]; // copied value, self-contained
};

static constexpr int MAX_PROP_ENTRIES = 48;
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

    // --- Fingerprint (all partitions) ---
    add("ro.build.fingerprint",          c.fingerprint);
    add("ro.system.build.fingerprint",   c.fingerprint);
    add("ro.vendor.build.fingerprint",   c.fingerprint);
    add("ro.odm.build.fingerprint",      c.fingerprint);
    add("ro.product.build.fingerprint",  c.fingerprint);

    // --- Serial ---
    add("ro.serialno",                   c.serial);
    add("ro.boot.serialno",             c.serial);
    add("sys.serialnumber",             c.serial);

    // --- Bootloader / Build IDs ---
    add("ro.bootloader",                c.bootloader);
    add("ro.build.display.id",          c.display);
    add("ro.build.id",                  c.build_id);
    add("ro.build.type",               c.build_type);
    add("ro.build.tags",               c.build_tags);
    add("ro.build.user",               c.build_user);
    add("ro.build.host",               c.build_host);
    add("ro.build.version.incremental", c.incremental);

    // --- Radio / Telephony ---
    add("gsm.version.baseband",         c.baseband);
    add("gsm.sim.operator.alpha",       c.operator_name);
    add("gsm.sim.operator.numeric",     c.operator_numeric);

    // --- Hardware ---
    add("ro.sf.lcd_density",            c.screen_density);
    add("ro.product.cpu.abi",           c.cpu_abi);
    add("ro.boot.hardware.revision",    c.hardware_serial);
    add("ro.soc.model",                c.soc_model);

    LOGI("prop_map: %d entries", g_prop_map_count);
}

// ============================================================================
// Hook: __system_property_get
// ============================================================================
typedef int (*orig_system_property_get_t)(const char *name, char *value);
static orig_system_property_get_t orig_system_property_get = nullptr;

static int proxy_system_property_get(const char *name, char *value) {
    // Call original first
    int ret = orig_system_property_get(name, value);

    // Intercept spoofed properties
    for (int i = 0; i < g_prop_map_count; i++) {
        if (strcmp(name, g_prop_map[i].key) == 0) {
            const size_t len = strlen(g_prop_map[i].value);
            memcpy(value, g_prop_map[i].value, len + 1);
            return static_cast<int>(len);
        }
    }

    return ret;
}

// ============================================================================
// Hook: open() — FIXED variadic forwarding with va_arg for mode_t
// ============================================================================
typedef int (*orig_open_t)(const char *pathname, int flags, ...);
static orig_open_t orig_open = nullptr;

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

// Use memfd_create for spoofed file content (no SELinux issues, no disk trace)
static int make_spoof_fd(const std::string &content) {
    // Try memfd_create first (no disk trace, no SELinux issues)
    int fd = -1;
#ifdef __NR_memfd_create
    fd = static_cast<int>(syscall(__NR_memfd_create, "jit-cache", 0u));
#endif
    if (fd < 0) {
        // Fallback: pipe-based approach
        int pipefd[2];
        if (pipe(pipefd) < 0) return -1;
        write(pipefd[1], content.c_str(), content.size())    ;
        write(pipefd[1], "\n", 1);
        close(pipefd[1]);
        return pipefd[0];
    }

    // memfd path
    if (fd >= 0) {
        std::string line = content + "\n";
        write(fd, line.c_str(), line.size());
        lseek(fd, 0, SEEK_SET);
    }
    return fd;
}

static int proxy_open(const char *pathname, int flags, ...) {
    // Extract mode_t (third arg) — CRITICAL for O_CREAT correctness
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }

    // Hide tracking directories
    if (should_hide_path(pathname)) {
        errno = ENOENT;
        return -1;
    }

    // WiFi MAC spoof
    if (!g_config.mac_address.empty()) {
        if (strcmp(pathname, "/sys/class/net/wlan0/address") == 0 ||
            strcmp(pathname, "/sys/class/net/eth0/address") == 0) {
            int fd = make_spoof_fd(g_config.mac_address);
            if (fd >= 0) return fd;
        }
    }

    // Bluetooth MAC spoof
    if (!g_config.bluetooth_mac.empty()) {
        if (strcmp(pathname, "/sys/class/bluetooth/hci0/address") == 0 ||
            strstr(pathname, "bt_addr") != nullptr) {
            int fd = make_spoof_fd(g_config.bluetooth_mac);
            if (fd >= 0) return fd;
        }
    }

    // Forward to original with correct mode_t
    if (flags & (O_CREAT | O_TMPFILE)) {
        return orig_open(pathname, flags, mode);
    }
    return orig_open(pathname, flags);
}

// ============================================================================
// Stealth: Scrub /proc/self/maps of ShadowHook RWX trampolines
// This makes inline hook trampolines invisible to memory scanner
// ============================================================================
static void scrub_maps_footprint() {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        // ShadowHook allocates anonymous RWX pages for trampolines
        // Pattern: <addr>-<addr> rwxp 00000000 00:00 0
        if (strstr(line, "rwxp") && strstr(line, " 00:00 0")) {
            unsigned long start = 0, end = 0;
            if (sscanf(line, "%lx-%lx", &start, &end) == 2 && start < end) {
                size_t size = end - start;
                // Re-mprotect to RX (removes W flag from maps readout)
                // Trampolines are already written, W is no longer needed
                mprotect(reinterpret_cast<void *>(start), size, PROT_READ | PROT_EXEC);
                LOGI("Scrubbed rwxp: %lx-%lx", start, end);
            }
        }
    }
    fclose(fp);
}

// ============================================================================
// JNI hook — simplified, no longer overwrites g_config
// ============================================================================
void install_jni_hooks(JNIEnv *env, const SpoofConfig &config) {
    (void)env;
    (void)config;
    // JNI-level hooks (e.g. Settings.Secure) can be added here in the future.
    // Native property hooks already cover ANDROID_ID and GSF_ID.
}

// ============================================================================
// Public API
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

    // Hook __system_property_get
    if (g_prop_map_count > 0) {
        prop_hook_stub = shadowhook_hook_sym_name(
            "libc.so", "__system_property_get",
            reinterpret_cast<void *>(proxy_system_property_get),
            reinterpret_cast<void **>(&orig_system_property_get)
        );
        if (!prop_hook_stub) {
            LOGE("Hook prop failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));
        }
    }

    // Hook open()
    open_hook_stub = shadowhook_hook_sym_name(
        "libc.so", "open",
        reinterpret_cast<void *>(proxy_open),
        reinterpret_cast<void **>(&orig_open)
    );
    if (!open_hook_stub) {
        LOGE("Hook open failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // Stealth: remove RWX pages from /proc/self/maps
    scrub_maps_footprint();

    g_hooks_installed = true;
    LOGI("Hooks installed: %d props + open", g_prop_map_count);
}
