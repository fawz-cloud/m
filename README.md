# Zygisk Device Spoofer

Universal device identity spoofer with WebUI for KernelSU Next / APatch / Magisk.

## Features
- **One-click "Burn & Spoof"** — Wipe app data + generate new device identity
- **Universal app targeting** — Add any app by package name
- **ShadowHook (ByteDance)** — Native inline hooking, maximum stealth
- **Built-in WebUI** — Configure from your root manager (no extra app)
- **Deep trace cleaning** — Removes 20+ analytics/tracking directories
- **Security-hardened** — Stack protector, FORTIFY_SOURCE, RELRO, LTO, maps scrubbing

## Installation
1. Download `zygisk-spoofer-release.zip` from [Releases](../../releases)
2. Flash via root manager → Reboot
3. Open root manager → Modules → tap module for WebUI

## Building
Push to `main` or tag for GitHub Actions build. Manual:
```bash
git clone --depth 1 https://github.com/bytedance/android-inline-hook.git external/shadowhook
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release .
cmake --build build -j$(nproc)
```
