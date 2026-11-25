#!/usr/bin/env bash
# Replaces the installed program with this published copy. Data is kept.
set -euo pipefail

if [ ! -f /etc/systemd/system/thermostat-server.service ]; then
    echo "Error: The service is not installed. Run install.sh first." >&2
    exit 1
fi
exec bash "$(dirname "${BASH_SOURCE[0]}")/install.sh"
