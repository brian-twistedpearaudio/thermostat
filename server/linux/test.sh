#!/usr/bin/env bash
# For CI only: runs the published copy, then installs, updates and uninstalls the service.
set -euo pipefail

[ "${GITHUB_ACTIONS:-}" = true ] || { echo "Only for a throwaway CI machine." >&2; exit 1; }
published=$(realpath "$1")
tmp=$(mktemp -d)
data=/var/lib/thermostat-server

check() {  # base address, token
    for _ in {1..20}; do curl -sf "$1/api/v1/health" >/dev/null && break; sleep 1; done
    curl -sf "$1/api/v1/health" | grep -q '"status":"ok"'
    curl -sf "$1/" >/dev/null
    [ "$(curl -s -o /dev/null -w '%{http_code}' -H 'X-Api-Token: bad' "$1/api/v1/devices")" = 401 ]
    if [ -n "${2:-}" ]; then curl -sf -H "X-Api-Token: $2" "$1/api/v1/devices"; fi
}
refused() { if "$@"; then echo "Not refused: $*" >&2; exit 1; fi; }

# From another folder, with its own data.
(cd / && exec "$published/Thermostat.Server" --urls http://127.0.0.1:5098 --DataFolder "$tmp/data") &
pid=$!
trap 'kill $pid 2>/dev/null || true; sudo journalctl -u thermostat-server --no-pager -n 50 || true' EXIT
check http://127.0.0.1:5098
token=$(cat "$tmp/data/token.txt")
device=0123456789ab
curl -sf -H "X-Api-Token: $token" --json \
    "{\"device\":\"$device\",\"name\":\"Test\",\"readings\":[]}" http://127.0.0.1:5098/api/v1/readings
# The database is in use.
refused sudo bash "$published/linux/install.sh" --copy-from "$tmp/data"
kill $pid
wait $pid || true

has_device() { check http://127.0.0.1:5080 "$token" | grep -q $device; }

refused sudo bash "$published/linux/install.sh"
sudo bash "$published/linux/install.sh" --copy-from "$tmp/data"
has_device
[ "$(sudo stat -c '%U %a' $data)" = "thermostat-server 700" ]
[ "$(sudo stat -c '%U %a' $data/token.txt)" = "thermostat-server 600" ]
[ "$(sudo stat -c '%U %a' $data/thermostat.db)" = "thermostat-server 600" ]
systemctl is-enabled thermostat-server
refused sudo bash "$published/linux/install.sh" --new-data

# Restarted after a failure, then updated.
old=$(systemctl show -P MainPID thermostat-server)
sudo kill -9 "$old"
sleep 15
[ "$(systemctl show -P MainPID thermostat-server)" != "$old" ]
has_device
sudo bash "$published/linux/update.sh"
has_device

# Data stays through uninstall and install.
sudo bash "$published/linux/uninstall.sh"
sudo test -f $data/thermostat.db
sudo bash "$published/linux/install.sh"
has_device

# No new, empty data if the folder goes missing.
sudo systemctl stop thermostat-server
sudo mv $data $data.away
refused sudo systemctl start thermostat-server
sudo test ! -e $data
sudo mv $data.away $data

sudo bash "$published/linux/uninstall.sh"
echo "Passed."
