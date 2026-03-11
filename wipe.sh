#!/system/bin/sh
# Zygisk Device Spoofer - Universal Wipe Script

MODDIR="${0%/*}"
CONFIG_FILE="/data/adb/modules/zygisk_spoofer/config.json"
SDCARD="/storage/emulated/0"

KNOWN_TRACKING_DIRS=".umeng Tencent .appsflyer .adjust .facebook .crashlytics .firebase .mobileapptracker .kochava .branch .airbridge .mixpanel .flurry .amplitude .bugly .huawei MiPushService .jpush .getui .push_deviceid"

log() { echo "[Spoofer Wipe] $1"; }

wipe_app() {
  local pkg="$1"
  if [ -z "$pkg" ]; then log "ERROR: No package name"; return 1; fi

  log "=== Wiping: $pkg ==="

  am force-stop "$pkg" 2>/dev/null
  pm clear "$pkg" 2>/dev/null

  rm -rf "${SDCARD}/Android/data/${pkg}" 2>/dev/null
  rm -rf "${SDCARD}/Android/media/${pkg}" 2>/dev/null
  rm -rf "${SDCARD}/Android/obb/${pkg}" 2>/dev/null

  for dir in $KNOWN_TRACKING_DIRS; do
    [ -d "${SDCARD}/${dir}" ] && rm -rf "${SDCARD}/${dir}" && log "  Removed: ${dir}"
  done

  local pkg_short=$(echo "$pkg" | sed 's/.*\.//')
  [ -d "${SDCARD}/.${pkg_short}" ] && rm -rf "${SDCARD}/.${pkg_short}"

  if [ -f "$CONFIG_FILE" ]; then
    # [M4] Parse custom_wipe_dirs from multiline JSON using awk
    local custom_dirs=$(awk '
      /"custom_wipe_dirs"/ { found=1; next }
      found && /\]/ { found=0 }
      found { gsub(/[",[:space:]]/, ""); if (length > 0) print }
    ' "$CONFIG_FILE")
    for custom_dir in $custom_dirs; do
      [ -e "${SDCARD}/${custom_dir}" ] && rm -rf "${SDCARD}/${custom_dir}" && log "  Custom: ${custom_dir}"
    done
  fi

  rm -rf "/data/data/com.google.android.gms/shared_prefs/adid_settings.xml" 2>/dev/null
  rm -rf "/data/data/com.google.android.gms/shared_prefs/AdvertisingIdNotification.xml" 2>/dev/null

  log "=== Done: $pkg ==="
}

if [ $# -eq 0 ]; then log "Usage: wipe.sh <pkg> [pkg2] ..."; exit 1; fi
for pkg in "$@"; do wipe_app "$pkg"; done
log "All wipe operations completed."
