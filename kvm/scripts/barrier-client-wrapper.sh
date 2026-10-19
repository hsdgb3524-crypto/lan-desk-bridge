#!/usr/bin/env bash
# ==============================================================================
# Barrier Client Smart Wrapper (Display Auto-Detection, Screen Wakeup, Failover)
# ==============================================================================

# 1. Dynamically detect active X11 display and XAUTHORITY from active user session
SHELL_PID=$(pgrep -u "$USER" -x gnome-shell 2>/dev/null | head -n 1)
if [ -n "$SHELL_PID" ] && [ -r "/proc/$SHELL_PID/environ" ]; then
    USER_DISP=$(grep -z '^DISPLAY=' "/proc/$SHELL_PID/environ" 2>/dev/null | cut -d= -f2- | tr -d '\0')
    USER_AUTH=$(grep -z '^XAUTHORITY=' "/proc/$SHELL_PID/environ" 2>/dev/null | cut -d= -f2- | tr -d '\0')
    [ -n "$USER_DISP" ] && export DISPLAY="$USER_DISP"
    [ -n "$USER_AUTH" ] && export XAUTHORITY="$USER_AUTH"
fi

# Fallback detection if session query did not provide valid DISPLAY
if [ -z "$DISPLAY" ] || [ "$DISPLAY" = ":0" ]; then
    if [ -S /tmp/.X11-unix/X1 ]; then
        export DISPLAY=":1"
    elif [ -S /tmp/.X11-unix/X0 ]; then
        export DISPLAY=":0"
    else
        export DISPLAY=":1"
    fi
fi

# Fallback detection for XAUTHORITY
if [ -z "$XAUTHORITY" ] || [ ! -r "$XAUTHORITY" ]; then
    UID_NUM=$(id -u)
    if [ -r "/run/user/${UID_NUM}/gdm/Xauthority" ]; then
        export XAUTHORITY="/run/user/${UID_NUM}/gdm/Xauthority"
    elif [ -r "$HOME/.Xauthority" ]; then
        export XAUTHORITY="$HOME/.Xauthority"
    fi
fi

# Ensure valid DBUS session bus address for GNOME calls
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ]; then
    UID_NUM=$(id -u)
    if [ -S "/run/user/${UID_NUM}/bus" ]; then
        export DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${UID_NUM}/bus"
    fi
fi

# Verify X11 server accessibility; if invalid, wait and exit for systemd to restart
if ! xset q >/dev/null 2>&1; then
    echo "[Warning] Cannot connect to X server on DISPLAY=${DISPLAY} (XAUTHORITY=${XAUTHORITY}). Retrying..."
    sleep 2
    exit 1
fi

SERVER_HOST="${1:-169.254.123.131}"
SERVER_PORT="${2:-24800}"
CLIENT_NAME="${3:-aa-MECHREVO}"

echo "Starting Barrier client on DISPLAY=${DISPLAY} (XAUTHORITY=${XAUTHORITY}) connecting to ${SERVER_HOST}:${SERVER_PORT}..."

LAST_RESET=0
FAIL_COUNT=0

# Stream processing: --debug DEBUG captures 'entering screen' without mouse motion spam
while IFS= read -r line; do
    echo "$line"

    # Reset failure counter upon successful connection
    if [[ "$line" == *"connected to server"* ]]; then
        FAIL_COUNT=0

    # Screen transition: instantly wake monitor, dismiss screensaver, release modifiers
    elif [[ "$line" == *"entering screen"* ]]; then
        # 1. Instantly wake monitor from DPMS sleep / black screen
        /usr/bin/xset dpms force on 2>/dev/null
        # 2. Dismiss GNOME screensaver / screen shield if active
        gdbus call --session --dest org.gnome.ScreenSaver --object-path /org/gnome/ScreenSaver --method org.gnome.ScreenSaver.SetActive false >/dev/null 2>&1
        # 3. Nudge pointer to trigger Mutter compositor refresh
        /usr/bin/xdotool mousemove_relative 1 1 2>/dev/null
        /usr/bin/xdotool mousemove_relative -- -1 -1 2>/dev/null
        # 4. Instantly release any modifier keys passed during hotkey transition
        /usr/bin/xdotool keyup Super_L Super_R Alt_L Alt_R Control_L Control_R Shift_L Shift_R 2>/dev/null
        # 5. Fix CapsLock if stuck on in X11
        if /usr/bin/xset q 2>/dev/null | grep -q "Caps Lock:   on"; then
            /usr/bin/xdotool key Caps_Lock 2>/dev/null
        fi

    # Server ghost connection detection (typically happens after host sleep/resume)
    elif [[ "$line" == *"server already has a connected client"* ]]; then
        NOW=$(date +%s)
        if (( NOW - LAST_RESET > 8 )); then
            LAST_RESET=$NOW
            FAIL_COUNT=0
            echo "[Auto-Recovery] Windows Barrier server holds stale connection. Sending remote refresh..."
            ssh -o ConnectTimeout=2 -o BatchMode=yes -o StrictHostKeyChecking=no win \
                "powershell.exe -NoProfile -Command \"Stop-Process -Name barriers -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 400; schtasks.exe /run /tn BarrierServer\"" >/dev/null 2>&1 &
        fi

    # Continuous connection failure watchdog (handles Connection refused, Timed out, server is dead)
    elif [[ "$line" == *"failed to connect to server"* ]] || [[ "$line" == *"server is dead"* ]]; then
        ((FAIL_COUNT++))
        if (( FAIL_COUNT >= 6 )); then
            NOW=$(date +%s)
            if (( NOW - LAST_RESET > 10 )); then
                LAST_RESET=$NOW
                FAIL_COUNT=0
                echo "[Auto-Recovery] Windows Barrier server unreachable/dead for 6s. Remotely restarting server..."
                ssh -o ConnectTimeout=2 -o BatchMode=yes -o StrictHostKeyChecking=no win \
                    "powershell.exe -NoProfile -Command \"Stop-Process -Name barriers -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 400; schtasks.exe /run /tn BarrierServer\"" >/dev/null 2>&1 &
            fi
        fi

    # Screen / MIT cookie error: exit immediately so systemd restarts with refreshed cookie
    elif [[ "$line" == *"secondary screen unavailable"* ]] || [[ "$line" == *"Invalid MIT-MAGIC-COOKIE"* ]]; then
        echo "[Auto-Recovery] Display authorization changed. Exiting for clean restart..."
        killall -9 barrierc 2>/dev/null
        exit 1
    fi
done < <(exec /usr/bin/stdbuf -oL -eL /usr/bin/barrierc --disable-crypto -f --no-tray --debug DEBUG --name "${CLIENT_NAME}" "${SERVER_HOST}:${SERVER_PORT}" 2>&1)
