#!/usr/bin/env bash
# One-Click Linux Setup for Dual-PC Bridge (KVM + Low-Latency Audio)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================================="
echo " Installing Dual-PC Bridge (KVM + Audio) on Linux"
echo "========================================================="

echo "[1/5] Checking dependencies (barrier, xdotool, xset, libpulse)..."
MISSING_PKGS=()
for pkg in barrier xdotool xset; do
    if ! command -v "$pkg" >/dev/null 2>&1; then
        MISSING_PKGS+=("$pkg")
    fi
done

if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    echo "Installing missing packages: ${MISSING_PKGS[*]}..."
    sudo apt-get update && sudo apt-get install -y "${MISSING_PKGS[@]}" libpulse-dev
fi

echo "[2/5] Building Audio Sender..."
cd "$REPO_DIR"
make sender

echo "[3/5] Installing binaries and wrapper scripts to /usr/local/bin..."
sudo cp "$REPO_DIR/bin/lan-audio-sender" /usr/local/bin/lan-audio-sender
sudo chmod 755 /usr/local/bin/lan-audio-sender

sudo cp "$REPO_DIR/kvm/scripts/barrier-client-wrapper.sh" /usr/local/bin/barrier-client-wrapper.sh
sudo chmod 755 /usr/local/bin/barrier-client-wrapper.sh

echo "[4/5] Installing systemd user services..."
mkdir -p "$HOME/.config/systemd/user"
cp "$REPO_DIR/kvm/service/barrier-client.service" "$HOME/.config/systemd/user/barrier-client.service"
cp "$REPO_DIR/scripts/lan-audio-sender.service" "$HOME/.config/systemd/user/lan-audio-sender.service"

systemctl --user daemon-reload
systemctl --user enable barrier-client.service lan-audio-sender.service
systemctl --user restart barrier-client.service lan-audio-sender.service

echo "[5/5] Verifying service status..."
systemctl --user status barrier-client.service lan-audio-sender.service --no-pager

echo "========================================================="
echo " Dual-PC Bridge installed and running successfully!"
echo " Hotkeys:"
echo "   Ctrl + Alt + K : Switch to Linux"
echo "   Ctrl + Alt + J : Return to Windows"
echo " Audio:"
echo "   All Linux desktop sounds stream to Windows headphones (<10ms)"
echo "========================================================="
