#!/system/bin/sh

# Zygisk Device Spoofer - Installation Script
SKIPUNZIP=1

ui_print "========================================"
ui_print "   Zygisk Device Spoofer v1.0.0"
ui_print "   Universal Identity Spoofer"
ui_print "========================================"

ui_print "- Extracting module files..."
unzip -o "$ZIPFILE" -x 'META-INF/*' -d "$MODPATH" >&2

ui_print "- Setting permissions..."
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm_recursive "$MODPATH/webroot" 0 0 0755 0644

if [ -f "$MODPATH/wipe.sh" ]; then
  set_perm "$MODPATH/wipe.sh" 0 0 0755
fi

for ABI in arm64-v8a armeabi-v7a x86 x86_64; do
  if [ -f "$MODPATH/zygisk/${ABI}.so" ]; then
    set_perm "$MODPATH/zygisk/${ABI}.so" 0 0 0644
  fi
done

CONFIG_DIR="/data/adb/modules/zygisk_spoofer"
CONFIG_FILE="${CONFIG_DIR}/config.json"

if [ ! -f "$CONFIG_FILE" ]; then
  ui_print "- Creating default config..."
  mkdir -p "$CONFIG_DIR"
  cat > "$CONFIG_FILE" << 'EOF'
{
  "target_apps": [],
  "spoof_values": {},
  "custom_wipe_dirs": [],
  "enabled": true
}
EOF
  chmod 0644 "$CONFIG_FILE"
fi

ui_print "- Installation complete!"
ui_print "- Open WebUI in your Root Manager to configure."
ui_print "========================================"
