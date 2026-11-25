#!/usr/bin/env bash
# Removes the service and its program files. The data folder and the account that owns it are kept.
set -euo pipefail

name=thermostat-server
unit=/etc/systemd/system/$name.service

if [ "$(id -u)" != 0 ]; then
    echo "Error: Run this with sudo." >&2
    exit 1
fi
if [ -f "$unit" ]; then
    systemctl disable --now $name
    rm "$unit"
    systemctl daemon-reload
fi
rm -rf "/opt/$name"

echo "The service is removed. Readings and token are kept in /var/lib/$name"
echo "No firewall rule was changed. Remove any rule you added for it."
