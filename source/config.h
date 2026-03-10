#pragma once

#include <string>
#include <vector>

struct SpoofConfig {
    bool enabled = false;
    std::vector<std::string> target_apps;
    
    // === Device Identity ===
    std::string android_id;
    std::string gsf_id;

    // === Network ===
    std::string mac_address;
    std::string bluetooth_mac;
    std::string wifi_ssid;
    std::string wifi_bssid;

    // === Build Properties ===
    std::string model;
    std::string brand;
    std::string manufacturer;
    std::string device;
    std::string product;
    std::string board;
    std::string hardware;
    std::string fingerprint;
    std::string serial;
    std::string bootloader;
    std::string display;
    std::string build_id;
    std::string build_type;
    std::string build_tags;
    std::string build_user;
    std::string build_host;
    std::string incremental;

    // === Radio / Telephony ===
    std::string imei;
    std::string meid;
    std::string baseband;
    std::string operator_name;
    std::string operator_numeric;

    // === Hardware Info ===
    std::string gl_renderer;
    std::string gl_vendor;
    std::string screen_density;
    std::string screen_resolution;
    std::string cpu_abi;
    std::string hardware_serial;
    std::string soc_model;

    std::vector<std::string> custom_wipe_dirs;
};

SpoofConfig load_config();
bool is_target_app(const SpoofConfig &config, const std::string &package_name);
