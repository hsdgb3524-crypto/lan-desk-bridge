#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== Building LAN Audio Sender ==="
cd "$REPO_DIR"
make sender

echo "=== Installing binary to /usr/local/bin ==="
sudo cp "$REPO_DIR/bin/lan-audio-sender" /usr/local/bin/lan-audio-sender
sudo chmod 755 /usr/local/bin/lan-audio-sender

echo "=== Installing systemd user service ==="
mkdir -p "$HOME/.config/systemd/user"
cp "$SCRIPT_DIR/lan-audio-sender.service" "$HOME/.config/systemd/user/lan-audio-sender.service"

systemctl --user daemon-reload
systemctl --user enable lan-audio-sender.service
systemctl --user restart lan-audio-sender.service

echo "=== Status ==="
systemctl --user status lan-audio-sender.service --no-pager
echo "=== Done! ==="
