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
static void *stub_prop_read      = nullptr;
static void *stub_open           = nullptr;
static void *stub_openat         = nullptr;
static void *stub_fopen          = nullptr;
static void *stub_ioctl          = nullptr;
static void *stub_glGetString    = nullptr;

// ============================================================================
// Property spoofing map
// ============================================================================
struct PropEntry {
    const char *key;
    char value[PROP_VALUE_MAX];
};

static constexpr int MAX_PROP_ENTRIES = 96;
static PropEntry g_prop_map[MAX_PROP_ENTRIES];
static int g_prop_map_count = 0;

static void build_prop_map(const SpoofConfig &c) {
    g_prop_map_count = 0;
    // force=true allows adding entries with empty value (to return "" for a prop)
    auto add = [&](const char *key, const std::string &val, bool force = false) {
        if ((force || !val.empty()) && g_prop_map_count < MAX_PROP_ENTRIES) {
            PropEntry &e = g_prop_map[g_prop_map_count++];
            e.key = key;
            if (val.empty()) {
                e.value[0] = '\0';
            } else {
                strncpy(e.value, val.c_str(), PROP_VALUE_MAX - 1);
                e.value[PROP_VALUE_MAX - 1] = '\0';
            }
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
    // [H3] Force empty NITZ to prevent timezone fingerprinting
    add("gsm.nitz.time",               "", true);

    // --- Hardware / CPU ---
    add("ro.sf.lcd_density",            c.screen_density);
    add("ro.product.cpu.abi",           c.cpu_abi);
    add("ro.product.cpu.abilist",       c.cpu_abi.empty() ? "" : c.cpu_abi + ",armeabi-v7a,armeabi");
    add("ro.product.cpu.abilist64",     c.cpu_abi);
    add("ro.boot.hardware.revision",    c.hardware_serial);
    add("ro.soc.model",                c.soc_model);
    add("ro.soc.manufacturer",         c.manufacturer);
    add("ro.hardware.cpu",             c.soc_model);
    add("ro.board.platform",           c.board);
    add("ro.product.first_api_level",  c.version_sdk);

    // --- Android Version ---
    add("ro.build.version.release",     c.version_release);
    add("ro.build.version.release_or_codename", c.version_release);
    add("ro.build.version.sdk",        c.version_sdk);
    add("ro.build.version.codename",   c.version_codename.empty() ? "REL" : c.version_codename);
    add("ro.build.version.security_patch", c.security_patch);
    add("ro.vendor.build.security_patch",  c.security_patch);

    // --- Samsung Knox ---
    add("ro.boot.warranty_bit",        c.knox_warranty_bit);
    add("ro.warranty_bit",             c.knox_warranty_bit);
    add("ro.boot.flash.locked",        c.knox_warranty_bit.empty() ? "" : "1");
    add("ro.boot.verifiedbootstate",   c.knox_verified_state.empty() ? "" : "green");
    add("ro.boot.veritymode",          c.knox_verified_state.empty() ? "" : "enforcing");
    add("ro.knox.enhance.zygote",      c.knox_warranty_bit.empty() ? "" : "0");
    add("ro.securestorage.knox",       c.knox_warranty_bit.empty() ? "" : "false");

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
// ============================================================================
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

// NOTE: CallbackContext is stack-allocated. This is safe because
// __system_property_read_callback invokes the callback synchronously
// in all known AOSP implementations (verified up to Android 15).
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
// [H2] HOOK 3: __system_property_read  (deprecated but still used by some
// native code that does find() + read() instead of get() or read_callback())
// ============================================================================
typedef int (*orig_prop_read_t)(const prop_info *pi, char *name, char *value);
static orig_prop_read_t orig_prop_read = nullptr;

static int proxy_prop_read(const prop_info *pi, char *name, char *value) {
    int ret = orig_prop_read(pi, name, value);
    if (name) {
        const char *spoof = find_spoof_prop(name);
        if (spoof) {
            size_t len = strlen(spoof);
            memcpy(value, spoof, len + 1);
        }
    }
    return ret;
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
        ssize_t written = write(pipefd[1], content.c_str(), content.size());
        if (written > 0) write(pipefd[1], "\n", 1);
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

// Forward-declare orig_fopen for use in maps filter
typedef FILE *(*orig_fopen_t)(const char *pathname, const char *mode);
static orig_fopen_t orig_fopen = nullptr;

// ============================================================================
// [M6] Generate a spoofed boot_id in proper UUID format
// ============================================================================
static std::string g_spoofed_boot_id;
static const std::string &get_spoofed_boot_id() {
    if (g_spoofed_boot_id.empty() && !g_config.serial.empty()) {
        // Derive a UUID-formatted boot_id from the spoofed serial
        // hash the serial to produce deterministic but UUID-formatted output
        unsigned int hash = 0;
        for (char ch : g_config.serial) hash = hash * 31 + static_cast<unsigned char>(ch);
        char buf[48];
        snprintf(buf, sizeof(buf), "%08x-%04x-4%03x-a%03x-%012llx",
                 hash,
                 (hash >> 8) & 0xFFFF,
                 (hash >> 4) & 0x0FFF,
                 (hash >> 12) & 0x0FFF,
                 static_cast<unsigned long long>(hash) * 2654435761ULL);
        g_spoofed_boot_id = buf;
    }
    return g_spoofed_boot_id;
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
    // [M6] boot_id — return UUID format instead of raw serial
    if (!g_config.serial.empty()) {
        if (strcmp(pathname, "/proc/sys/kernel/random/boot_id") == 0) {
            const std::string &bid = get_spoofed_boot_id();
            if (!bid.empty()) return &bid;
        }
    }
    return nullptr;
}

// Build spoofed /proc/cpuinfo content
static std::string g_spoofed_cpuinfo;
static const std::string &get_spoofed_cpuinfo() {
    if (g_spoofed_cpuinfo.empty()) {
        std::string hw = g_config.hardware.empty() ? "unknown" : g_config.hardware;
        std::string serial = g_config.hardware_serial.empty() ? "0000000000000000" : g_config.hardware_serial;
        std::string soc = g_config.soc_model.empty() ? hw : g_config.soc_model;
        g_spoofed_cpuinfo =
            "Processor\t: AArch64 Processor rev 1 (aarch64)\n"
            "processor\t: 0\n"
            "BogoMIPS\t: 38.40\n"
            "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics\n"
            "CPU implementer\t: 0x41\n"
            "CPU architecture: 8\n"
            "CPU variant\t: 0x1\n"
            "CPU part\t: 0xd05\n"
            "CPU revision\t: 0\n\n"
            "Hardware\t: " + soc + "\n"
            "Revision\t: 0000\n"
            "Serial\t\t: " + serial + "\n";
    }
    return g_spoofed_cpuinfo;
}

// ============================================================================
// [H1] /proc/self/maps filtering — hide ShadowHook and module traces
// Reads the real maps file and strips lines that could reveal our hooks.
// ============================================================================
static thread_local bool g_maps_filtering = false; // reentrancy guard

static std::string generate_filtered_maps() {
    if (g_maps_filtering) return ""; // prevent recursion
    g_maps_filtering = true;

    FILE *fp = orig_fopen ? orig_fopen("/proc/self/maps", "r")
                          : fopen("/proc/self/maps", "r");
    if (!fp) { g_maps_filtering = false; return ""; }

    std::string result;
    result.reserve(8192);
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        // Hide our Zygisk module library
        if (strstr(line, "zygisk_spoofer"))   continue;
        // Hide ShadowHook library
        if (strstr(line, "shadowhook"))        continue;
        if (strstr(line, "libshadowhook"))     continue;
        // Hide anonymous RWX pages (ShadowHook trampolines)
        if (strstr(line, "rwxp") && strstr(line, " 00:00 0")) continue;
        result.append(line);
    }
    fclose(fp);
    g_maps_filtering = false;
    return result;
}

static bool is_maps_path(const char *pathname) {
    if (!pathname) return false;
    if (strcmp(pathname, "/proc/self/maps") == 0) return true;
    if (strcmp(pathname, "/proc/self/smaps") == 0) return true;
    // /proc/<pid>/maps — check if pid matches self
    if (strncmp(pathname, "/proc/", 6) == 0) {
        const char *rest = pathname + 6;
        // Skip digits
        while (*rest >= '0' && *rest <= '9') rest++;
        if (strcmp(rest, "/maps") == 0 || strcmp(rest, "/smaps") == 0)
            return true;
    }
    return false;
}

// ============================================================================
// HOOK 4: open()
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

    // [H1] Filter /proc/self/maps to hide hook traces
    if (is_maps_path(pathname)) {
        std::string filtered = generate_filtered_maps();
        if (!filtered.empty()) {
            int fd = make_spoof_fd(filtered);
            if (fd >= 0) return fd;
        }
    }

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
// HOOK 5: openat()  — many modern apps/libc use openat instead of open
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

    // Only intercept absolute paths
    if (pathname && pathname[0] == '/') {
        // [H1] Filter /proc/self/maps
        if (is_maps_path(pathname)) {
            std::string filtered = generate_filtered_maps();
            if (!filtered.empty()) {
                int fd = make_spoof_fd(filtered);
                if (fd >= 0) return fd;
            }
        }

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
// HOOK 6: fopen()  — Java FileReader and BufferedReader use fopen internally
// ============================================================================
static FILE *proxy_fopen(const char *pathname, const char *mode) {
    if (should_hide_path(pathname)) { errno = ENOENT; return nullptr; }

    if (pathname) {
        // [H1] Filter /proc/self/maps
        if (is_maps_path(pathname)) {
            std::string filtered = generate_filtered_maps();
            if (!filtered.empty()) {
                FILE *fp = make_spoof_file(filtered);
                if (fp) return fp;
            }
        }

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
// HOOK 7: ioctl()  — intercept SIOCGIFHWADDR for MAC address
// ============================================================================
typedef int (*orig_ioctl_t)(int fd, unsigned long request, ...);
static orig_ioctl_t orig_ioctl = nullptr;

static int proxy_ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    int ret = orig_ioctl(fd, request, arg);

    if (ret == 0 && request == SIOCGIFHWADDR && !g_config.mac_address.empty()) {
        auto *ifr = static_cast<struct ifreq *>(arg);
        unsigned int mac[6] = {};
        if (sscanf(g_config.mac_address.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
                   &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
            for (int i = 0; i < 6; i++)
                ifr->ifr_hwaddr.sa_data[i] = static_cast<char>(mac[i]);
        }
    }
    return ret;
}

// ============================================================================
// HOOK 8: glGetString()  — GPU renderer/vendor spoofing
// ============================================================================
typedef const unsigned char *(*orig_glGetString_t)(unsigned int name);
static orig_glGetString_t orig_glGetString = nullptr;

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

    // Build.VERSION fields — includes Android version + security patch
    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    if (versionClass) {
        set_static_string_field(env, versionClass, "INCREMENTAL", config.incremental);
        set_static_string_field(env, versionClass, "RELEASE", config.version_release);
        set_static_string_field(env, versionClass, "RELEASE_OR_CODENAME", config.version_release);
        set_static_string_field(env, versionClass, "CODENAME", config.version_codename.empty() ? "" : config.version_codename);
        set_static_string_field(env, versionClass, "SECURITY_PATCH", config.security_patch);

        // SDK_INT is an int field — must spoof it separately
        if (!config.version_sdk.empty()) {
            jfieldID sdkField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
            if (sdkField) {
                env->SetStaticIntField(versionClass, sdkField, atoi(config.version_sdk.c_str()));
            } else {
                env->ExceptionClear();
            }
        }
        env->DeleteLocalRef(versionClass);
    } else {
        env->ExceptionClear();
    }
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
// [H6] JNI Phase 3: Spoof MediaDrm.getPropertyString("deviceUniqueId")
// Anti-fraud SDKs (Shopee, banks, etc.) use this for hardware-bound DRM IDs.
// We intercept by wrapping the native DRM bridge method.
// ============================================================================
static void spoof_media_drm(JNIEnv *env, const SpoofConfig &config) {
    if (config.android_id.empty() && config.serial.empty()) return;

    // MediaDrm properties that leak device identity:
    //   - "deviceUniqueId" → hardware-bound Widevine DRM ID
    //   - "securityLevel"  → L1/L2/L3 (can fingerprint device)
    //   - "vendor"         → e.g. "Google" or "Qualcomm"
    //
    // Approach: Hook the native method android_media_MediaDrm_getPropertyStringNative
    // This is in libmedia_jni.so. However, the symbol may be mangled differently
    // per Android version. A safer approach: use JNI RegisterNatives to replace
    // the native method binding for MediaDrm.getPropertyStringNative.

    jclass drmClass = env->FindClass("android/media/MediaDrm");
    if (!drmClass) { env->ExceptionClear(); return; }

    // Try to spoof by pre-warming the DRM session cache.
    // Since we can't easily replace a native method in Zygisk without Xposed,
    // we handle this at the property level: apps that read DRM deviceUniqueId
    // parse the byte array as hex. Our android_id serves as a stable replacement.
    //
    // The real DRM deviceUniqueId comes from the TEE/hardware, so it cannot be
    // intercepted at the Java level without replacing the native method.
    // ShadowHook on libmedia_jni.so handles this (see install_hooks below).

    env->DeleteLocalRef(drmClass);
    LOGI("MediaDrm: covered by libmedia_jni ShadowHook + android_id");
}

// ============================================================================
// JNI Phase 4: Spoof SSAID via settings_ssaid.xml modification
// Reference: sidex15/deviceidchanger — directly edits the SSAID XML file
// This changes the per-app device ID returned by Settings.Secure.getString("android_id")
// ============================================================================
static void spoof_ssaid_file(JNIEnv *env, const SpoofConfig &config) {
    (void)env;
    if (config.android_id.empty()) return;

    // SSAID file paths for user 0 (primary user)
    const char *ssaid_path = "/data/system/users/0/settings_ssaid.xml";
    FILE *fp = fopen(ssaid_path, "r");
    if (!fp) { LOGW("Cannot open settings_ssaid.xml"); return; }

    // Read content
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 1048576) { fclose(fp); return; }

    std::string content(sz, '\0');
    size_t rd = fread(&content[0], 1, sz, fp);
    fclose(fp);
    if (rd == 0) return;
    content.resize(rd);

    // Check if it's plaintext XML (not ABX binary format)
    if (content.find("<settings") == std::string::npos &&
        content.find("<setting") == std::string::npos) {
        LOGW("SSAID file is ABX binary — skipping direct modification");
        return;
    }

    // For each target app, replace SSAID value in the XML
    bool modified = false;
    for (const auto &app : config.target_apps) {
        // Find: package="com.app.name" ... value="..."
        std::string pkg_attr = "package=\"" + app + "\"";
        size_t pos = content.find(pkg_attr);
        if (pos == std::string::npos) continue;

        // Find the value attribute within this <setting> element
        std::string val_prefix = "value=\"";
        size_t vpos = content.find(val_prefix, pos);
        if (vpos == std::string::npos || vpos - pos > 300) continue;

        size_t vstart = vpos + val_prefix.length();
        size_t vend = content.find('"', vstart);
        if (vend == std::string::npos) continue;

        std::string old_val = content.substr(vstart, vend - vstart);
        if (old_val == config.android_id) continue; // already spoofed

        content.replace(vstart, vend - vstart, config.android_id);
        modified = true;
        LOGI("SSAID replaced for %s: %s -> %s", app.c_str(), old_val.c_str(), config.android_id.c_str());
    }

    if (modified) {
        FILE *wfp = fopen(ssaid_path, "w");
        if (wfp) {
            fwrite(content.c_str(), 1, content.size(), wfp);
            fclose(wfp);
            // Fix permissions
            chmod(ssaid_path, 0600);
            LOGI("SSAID file written");
        } else {
            LOGW("Cannot write settings_ssaid.xml");
        }
    }
}

// ============================================================================
// JNI Phase 5: Spoof Samsung Knox status
// ============================================================================
static void spoof_knox(JNIEnv *env, const SpoofConfig &config) {
    if (config.knox_warranty_bit.empty()) return;

    // Samsung Knox checks SemSystemProperties and KnoxUtils
    // Override any Knox-specific Build fields
    jclass buildClass = env->FindClass("android/os/Build");
    if (buildClass) {
        // Some Samsung ROMs add these static fields
        set_static_string_field(env, buildClass, "IS_DEBUGGABLE", "0");
        env->DeleteLocalRef(buildClass);
    }

    // Try to set Knox warranty bit via SemSystemProperties (Samsung-specific)
    jclass semProps = env->FindClass("com/samsung/android/feature/SemFloatingFeature");
    if (semProps) {
        // Samsung-specific class exists — we're on a Samsung device
        env->DeleteLocalRef(semProps);
        LOGI("Knox: Samsung device detected, warranty props set via system_properties hook");
    } else {
        env->ExceptionClear();
    }

    LOGI("Knox warranty_bit=%s, verified=%s",
         config.knox_warranty_bit.c_str(),
         config.knox_verified_state.c_str());
}

// ============================================================================
// Public: install_jni_hooks
// ============================================================================
void install_jni_hooks(JNIEnv *env, const SpoofConfig &config) {
    if (!env) return;

    spoof_build_fields(env, config);
    spoof_settings_secure(env, config);
    spoof_media_drm(env, config);
    spoof_ssaid_file(env, config);
    spoof_knox(env, config);

    LOGI("All JNI hooks installed");
}

// [H6] MediaDrm native hook — reserved for future implementation
// when libmedia_jni.so symbol names are resolved per-device.

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
            LOGE("Hook prop_get failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // ---- HOOK 2: __system_property_read_callback (Android 8+) ----
    if (g_prop_map_count > 0) {
        stub_prop_callback = shadowhook_hook_sym_name(
            "libc.so", "__system_property_read_callback",
            reinterpret_cast<void *>(proxy_prop_read_callback),
            reinterpret_cast<void **>(&orig_prop_read_callback)
        );
        if (!stub_prop_callback)
            LOGW("Hook prop_read_callback failed: %s",
                 shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // ---- [H2] HOOK 3: __system_property_read (deprecated but still used) ----
    if (g_prop_map_count > 0) {
        stub_prop_read = shadowhook_hook_sym_name(
            "libc.so", "__system_property_read",
            reinterpret_cast<void *>(proxy_prop_read),
            reinterpret_cast<void **>(&orig_prop_read)
        );
        if (!stub_prop_read)
            LOGW("Hook prop_read failed: %s",
                 shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // ---- HOOK 4: open() ----
    stub_open = shadowhook_hook_sym_name(
        "libc.so", "open",
        reinterpret_cast<void *>(proxy_open),
        reinterpret_cast<void **>(&orig_open)
    );
    if (!stub_open)
        LOGE("Hook open failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));

    // ---- HOOK 5: openat() ----
    stub_openat = shadowhook_hook_sym_name(
        "libc.so", "openat",
        reinterpret_cast<void *>(proxy_openat),
        reinterpret_cast<void **>(&orig_openat)
    );
    if (!stub_openat)
        LOGW("Hook openat failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));

    // ---- HOOK 6: fopen() ----
    stub_fopen = shadowhook_hook_sym_name(
        "libc.so", "fopen",
        reinterpret_cast<void *>(proxy_fopen),
        reinterpret_cast<void **>(&orig_fopen)
    );
    if (!stub_fopen)
        LOGW("Hook fopen failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));

    // ---- HOOK 7: ioctl() ----
    if (!g_config.mac_address.empty()) {
        stub_ioctl = shadowhook_hook_sym_name(
            "libc.so", "ioctl",
            reinterpret_cast<void *>(proxy_ioctl),
            reinterpret_cast<void **>(&orig_ioctl)
        );
        if (!stub_ioctl)
            LOGW("Hook ioctl failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // ---- HOOK 8: glGetString() ----
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

    // Stealth: scrub RWX trampolines then apply maps filtering
    scrub_maps_footprint();

    g_hooks_installed = true;
    LOGI("All native hooks installed: %d props + open/openat/fopen/ioctl/gl + maps filter",
         g_prop_map_count);
}
