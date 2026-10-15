#!/usr/bin/env bash
# Restart both Barrier KVM and LAN Audio services on Linux
systemctl --user restart barrier-client.service lan-audio-sender.service
echo "Restarted barrier-client.service and lan-audio-sender.service."
systemctl --user status barrier-client.service lan-audio-sender.service --no-pager
