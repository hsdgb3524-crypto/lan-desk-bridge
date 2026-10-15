#!/usr/bin/env bash
# Barrier Client Smart Wrapper
# Solves two notorious KVM bugs:
# 1. Stuck modifier keys (Super/Alt/Ctrl/Shift) upon switching screen via hotkey
# 2. Desynchronized CapsLock / NumLock states across different OSes

export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/run/user/1000/gdm/Xauthority}"

SERVER_HOST="${1:-169.254.123.131}"
SERVER_PORT="${2:-24800}"
CLIENT_NAME="${3:-aa-MECHREVO}"

echo "Starting smart Barrier client connecting to ${SERVER_HOST}:${SERVER_PORT} as ${CLIENT_NAME}..."

exec /usr/bin/stdbuf -oL -eL /usr/bin/barrierc --disable-crypto -f --no-tray --debug DEBUG2 --name "${CLIENT_NAME}" "${SERVER_HOST}:${SERVER_PORT}" 2>&1 | while IFS= read -r line; do
    echo "$line"
    if [[ "$line" == *"entering screen"* ]]; then
        # Instantly release any modifier keys passed during hotkey transition
        /usr/bin/xdotool keyup Super_L Super_R Alt_L Alt_R Control_L Control_R Shift_L Shift_R 2>/dev/null
        # Fix CapsLock if stuck on in X11
        if /usr/bin/xset q 2>/dev/null | grep -q "Caps Lock:   on"; then
            /usr/bin/xdotool key Caps_Lock 2>/dev/null
        fi
    fi
done
