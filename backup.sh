#!/system/bin/sh
# Zygisk Device Spoofer - Profile Backup & Restore Script
# Usage:
#   backup.sh backup  <backup_dir> <profile_name> <pkg1> [pkg2] ...
#   backup.sh restore <backup_dir> <profile_name> <pkg1> [pkg2] ...
#   backup.sh list    <backup_dir>
#   backup.sh delete  <backup_dir> <profile_name>

SDCARD="/storage/emulated/0"

log() { echo "[Spoofer Profile] $1"; }

get_app_uid() {
  local pkg="$1"
  stat -c '%u' "/data/data/${pkg}" 2>/dev/null
}

# ============================================================================
# BACKUP
# ============================================================================
do_backup() {
  local BACKUP_DIR="$1"
  local PROFILE="$2"
  shift 2
  local PROFILE_DIR="${BACKUP_DIR}/${PROFILE}"

  mkdir -p "$PROFILE_DIR"

  for pkg in "$@"; do
    log "Backing up: $pkg"
    am force-stop "$pkg" 2>/dev/null

    # Backup /data/data/<pkg>
    if [ -d "/data/data/${pkg}" ]; then
      tar -czf "${PROFILE_DIR}/${pkg}.data.tar.gz" -C /data/data "${pkg}" 2>/dev/null
      if [ $? -eq 0 ]; then
        log "  /data/data/${pkg} -> OK"
      else
        log "  /data/data/${pkg} -> FAILED"
      fi
    fi

    # Backup /sdcard/Android/data/<pkg>
    if [ -d "${SDCARD}/Android/data/${pkg}" ]; then
      tar -czf "${PROFILE_DIR}/${pkg}.sdcard.tar.gz" -C "${SDCARD}/Android/data" "${pkg}" 2>/dev/null
      if [ $? -eq 0 ]; then
        log "  sdcard/Android/data/${pkg} -> OK"
      else
        log "  sdcard/Android/data/${pkg} -> FAILED"
      fi
    fi
  done

  # Save spoof config snapshot
  local CONFIG="/data/adb/modules/zygisk_spoofer/config.json"
  if [ -f "$CONFIG" ]; then
    cp "$CONFIG" "${PROFILE_DIR}/profile.json"
    log "Config saved to profile.json"
  fi

  # Save timestamp
  date '+%Y-%m-%d %H:%M:%S' > "${PROFILE_DIR}/.timestamp"

  log "=== Backup complete: ${PROFILE} ==="
}

# ============================================================================
# RESTORE
# ============================================================================
do_restore() {
  local BACKUP_DIR="$1"
  local PROFILE="$2"
  shift 2
  local PROFILE_DIR="${BACKUP_DIR}/${PROFILE}"

  if [ ! -d "$PROFILE_DIR" ]; then
    log "ERROR: Profile not found: ${PROFILE}"
    exit 1
  fi

  for pkg in "$@"; do
    log "Restoring: $pkg"
    am force-stop "$pkg" 2>/dev/null

    # Get UID before clearing (in case app is installed)
    local uid=$(get_app_uid "$pkg")

    # Clear app data first
    pm clear "$pkg" 2>/dev/null

    # Restore /data/data/<pkg>
    if [ -f "${PROFILE_DIR}/${pkg}.data.tar.gz" ]; then
      tar -xzf "${PROFILE_DIR}/${pkg}.data.tar.gz" -C /data/data/ 2>/dev/null
      if [ $? -eq 0 ]; then
        log "  /data/data/${pkg} <- restored"
        # Fix ownership
        if [ -n "$uid" ]; then
          chown -R "${uid}:${uid}" "/data/data/${pkg}" 2>/dev/null
          log "  ownership fixed (uid=${uid})"
        fi
        # Fix SELinux context
        restorecon -R "/data/data/${pkg}" 2>/dev/null
      else
        log "  /data/data/${pkg} <- FAILED"
      fi
    fi

    # Restore /sdcard/Android/data/<pkg>
    if [ -f "${PROFILE_DIR}/${pkg}.sdcard.tar.gz" ]; then
      mkdir -p "${SDCARD}/Android/data" 2>/dev/null
      tar -xzf "${PROFILE_DIR}/${pkg}.sdcard.tar.gz" -C "${SDCARD}/Android/data/" 2>/dev/null
      if [ $? -eq 0 ]; then
        log "  sdcard/Android/data/${pkg} <- restored"
      else
        log "  sdcard/Android/data/${pkg} <- FAILED"
      fi
    fi

    am force-stop "$pkg" 2>/dev/null
  done

  # Restore spoof config
  local CONFIG="/data/adb/modules/zygisk_spoofer/config.json"
  if [ -f "${PROFILE_DIR}/profile.json" ]; then
    cp "${PROFILE_DIR}/profile.json" "$CONFIG"
    chmod 644 "$CONFIG"
    log "Config restored from profile.json"
  fi

  # Patch SSAID from restored config
  local SSAID=$(awk -F'"' '/"android_id"/ {print $4}' "${CONFIG}" 2>/dev/null)
  if [ -n "$SSAID" ] && [ ${#SSAID} -eq 16 ]; then
    local SSAID_XML="/data/system/users/0/settings_ssaid.xml"
    if [ -f "$SSAID_XML" ]; then
      sed -i "s/value=\"[0-9a-fA-F]\{16\}\"/value=\"${SSAID}\"/g" "$SSAID_XML"
      chmod 600 "$SSAID_XML"
      log "SSAID patched: ${SSAID}"
    fi
    # Restart SettingsProvider to reload
    killall -9 com.android.providers.settings 2>/dev/null
  fi

  log "=== Restore complete: ${PROFILE} ==="
}

# ============================================================================
# LIST — output JSON array of profiles
# ============================================================================
do_list() {
  local BACKUP_DIR="$1"

  if [ ! -d "$BACKUP_DIR" ]; then
    echo "[]"
    return
  fi

  local first=1
  echo -n "["
  for dir in "${BACKUP_DIR}"/*/; do
    [ ! -d "$dir" ] && continue
    local name=$(basename "$dir")
    local ts=""
    [ -f "${dir}.timestamp" ] && ts=$(cat "${dir}.timestamp")
    
    # Count backup files
    local count=$(ls -1 "${dir}"*.tar.gz 2>/dev/null | wc -l)

    if [ $first -eq 1 ]; then first=0; else echo -n ","; fi
    echo -n "{\"name\":\"${name}\",\"date\":\"${ts}\",\"files\":${count}}"
  done
  echo "]"
}

# ============================================================================
# DELETE
# ============================================================================
do_delete() {
  local BACKUP_DIR="$1"
  local PROFILE="$2"
  local PROFILE_DIR="${BACKUP_DIR}/${PROFILE}"

  if [ ! -d "$PROFILE_DIR" ]; then
    log "ERROR: Profile not found: ${PROFILE}"
    exit 1
  fi

  rm -rf "$PROFILE_DIR"
  log "Deleted profile: ${PROFILE}"
}

# ============================================================================
# Main
# ============================================================================
ACTION="$1"
case "$ACTION" in
  backup)
    shift; do_backup "$@" ;;
  restore)
    shift; do_restore "$@" ;;
  list)
    shift; do_list "$@" ;;
  delete)
    shift; do_delete "$@" ;;
  *)
    log "Usage: backup.sh {backup|restore|list|delete} <backup_dir> <profile_name> [pkg ...]"
    exit 1 ;;
esac
