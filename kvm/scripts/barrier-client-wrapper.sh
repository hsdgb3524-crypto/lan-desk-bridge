#!/usr/bin/env bash
# Barrier Client Smart Wrapper (Auto-detecting display & Xauthority, optimized for 1:1 responsiveness)

# 1. Dynamically detect active X11 display
if [ -z "$DISPLAY" ] || [ "$DISPLAY" = ":0" ]; then
    DETECTED_DISP=$(ls /tmp/.X11-unix/ 2>/dev/null | grep '^X' | sed 's/^X/:/' | head -n 1)
    if [ -n "$DETECTED_DISP" ]; then
        export DISPLAY="$DETECTED_DISP"
    else
        export DISPLAY=":1"
    fi
fi

# 2. Dynamically detect valid XAUTHORITY
if [ -z "$XAUTHORITY" ] || [ ! -r "$XAUTHORITY" ]; then
    UID_NUM=$(id -u)
    if [ -r "/run/user/${UID_NUM}/gdm/Xauthority" ]; then
        export XAUTHORITY="/run/user/${UID_NUM}/gdm/Xauthority"
    elif [ -r "$HOME/.Xauthority" ]; then
        export XAUTHORITY="$HOME/.Xauthority"
    fi
fi

SERVER_HOST="${1:-169.254.123.131}"
SERVER_PORT="${2:-24800}"
CLIENT_NAME="${3:-aa-MECHREVO}"

echo "Starting Barrier client on DISPLAY=${DISPLAY} (XAUTHORITY=${XAUTHORITY}) connecting to ${SERVER_HOST}:${SERVER_PORT}..."

# Run barrierc with --debug INFO to suppress massive mouse event log spam, eliminating pipe buffer lag and CPU spikes
exec /usr/bin/stdbuf -oL -eL /usr/bin/barrierc --disable-crypto -f --no-tray --debug INFO --name "${CLIENT_NAME}" "${SERVER_HOST}:${SERVER_PORT}" 2>&1 | while IFS= read -r line; do
    if [[ "$line" == *"entering screen"* ]]; then
        # Instantly release any modifier keys (Super, Alt, Ctrl, Shift) passed during hotkey transition
        /usr/bin/xdotool keyup Super_L Super_R Alt_L Alt_R Control_L Control_R Shift_L Shift_R 2>/dev/null
        # Fix CapsLock if stuck on in X11
        if /usr/bin/xset q 2>/dev/null | grep -q "Caps Lock:   on"; then
            /usr/bin/xdotool key Caps_Lock 2>/dev/null
        fi
    fi
done
