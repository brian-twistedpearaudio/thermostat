#!/usr/bin/env bash
# Installs the thermostat server as a systemd service, or updates it. Safe to run again.
# Run it from a published copy: sudo bash linux/install.sh --copy-from FOLDER | --new-data
set -euo pipefail

name=thermostat-server
port=5080
root=/opt/$name
app=$root/app
data=/var/lib/$name
unit=/etc/systemd/system/$name.service
exe=Thermostat.Server
published=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)

fail() { echo "Error: $*" >&2; exit 1; }
exists() { [ -e "$1" ] || [ -L "$1" ]; }

copy_from=
new_data=
while [ $# -gt 0 ]; do
    case $1 in
        --copy-from) [ -n "${2:-}" ] || fail "--copy-from needs a folder."; copy_from=$2; shift 2 ;;
        --new-data) new_data=1; shift ;;
        *) fail "Unknown option $1. Use --copy-from FOLDER or --new-data." ;;
    esac
done

# ----- Checks: nothing is changed before these pass -----

[ "$(id -u)" = 0 ] || fail "Run this with sudo."
[ -d /run/systemd/system ] || fail "This system does not use systemd."
[ -z "$copy_from" ] || [ -z "$new_data" ] || fail "Use --copy-from or --new-data, not both."
[ -f "$published/$exe" ] && [ -f "$published/wwwroot/index.html" ] || fail "$published is not a copy published for Linux."

# The processor type in the program's header: 62 is x86-64, 183 is ARM64.
case $(uname -m) in
    x86_64) need=linux-x64 header=62 ;;
    aarch64) need=linux-arm64 header=183 ;;
    *) fail "This processor, $(uname -m), is not supported: only x86_64 and aarch64." ;;
esac
# 32-bit Raspberry Pi OS can have a 64-bit kernel, which says aarch64.
[ "$(getconf LONG_BIT)" = 64 ] || fail "This is a 32-bit system. A 64-bit one is needed."
machine=$(od -An -tu1 -j18 -N1 "$published/$exe" | tr -d " ")
[ "$machine" = "$header" ] || fail "This copy is for another processor. Publish with: -r $need"

[ ! -L "$data" ] || fail "$data is a link. Remove it first."
has_data=
if exists "$data/thermostat.db" && exists "$data/token.txt"; then
    has_data=1
elif exists "$data/thermostat.db" || exists "$data/token.txt"; then
    fail "$data has only one of thermostat.db and token.txt. Put the other back first."
fi
if [ -n "$has_data" ] && [ -n "$copy_from$new_data" ]; then
    fail "$data already holds data, which is never overwritten. Run again without --copy-from or --new-data."
fi
if [ -z "$has_data" ] && [ -z "$copy_from$new_data" ]; then
    fail "There is no data for the service yet. Use --copy-from FOLDER to keep your readings and token, or --new-data to start empty."
fi
if [ -n "$copy_from" ]; then
    copy_from=$(cd "$copy_from" && pwd -P)
    for file in thermostat.db token.txt; do
        [ -f "$copy_from/$file" ] || fail "$copy_from/$file was not found."
    done
    if find /proc/[0-9]*/fd -lname "$copy_from/thermostat.db" 2>/dev/null | grep -q .; then
        fail "$copy_from/thermostat.db is in use. Stop the server that uses it first."
    fi
fi

if ! systemctl is-active --quiet $name && (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
    fail "Port $port is in use. Stop the other server first."
fi

# ----- Copy the program beside the running one -----

mkdir -p "$root"
rm -rf "$app.new"
cp -r "$published" "$app.new"
chown -R root:root "$root"
chmod -R u=rwX,go=rX "$root"
chmod 755 "$app.new/$exe"
if command -v restorecon >/dev/null; then restorecon -R "$root"; fi

# ----- Account and data folder -----

if ! id -u $name >/dev/null 2>&1; then
    useradd --system --user-group --no-create-home --home-dir /nonexistent \
        --shell "$(command -v nologin || echo /bin/false)" $name
fi
if [ -f "$unit" ]; then systemctl stop $name; fi

install -d -m 700 -o $name -g $name "$data"

# Copied whole, then moved into place without overwriting. The original stays as it is.
if [ -n "$copy_from" ]; then
    incoming=$(mktemp -d "$data.XXXXXX")
    trap 'rm -rf "$incoming"' EXIT
    files="thermostat.db thermostat.db-wal token.txt"
    for file in $files; do
        [ -f "$copy_from/$file" ] || continue
        install -m 600 -o $name -g $name "$copy_from/$file" "$incoming/$file"
    done
    for file in $files; do
        [ -f "$incoming/$file" ] || continue
        ! exists "$data/$file" || fail "$data/$file already exists. It was not overwritten."
        mv -n "$incoming/$file" "$data/$file"
    done
fi

# ----- Program files and unit -----

rm -rf "$app.old"
if [ -d "$app" ]; then mv "$app" "$app.old"; fi
mv "$app.new" "$app"
install -m 644 "$app/linux/$name.service" "$unit"

# ----- Start -----

# Asked through bash, as the host may have no curl.
healthy() {
    timeout 3 bash -c '
        exec 3<>"/dev/tcp/127.0.0.1/$1"
        printf "GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" >&3
        grep -q "\"status\":\"ok\"" <&3' _ $port 2>/dev/null
}

systemctl daemon-reload
systemctl enable --quiet $name
ok=
if systemctl start $name; then
    for _ in {1..15}; do
        if healthy; then ok=1; break; fi
        sleep 1
    done
fi
[ -n "$ok" ] || fail "The service did not answer on port $port. See: journalctl -u $name. The previous program, if any, is in $app.old."
rm -rf "$app.old"

echo
echo "The thermostat server is running and starts with the system. Data: $data"
echo "To see the token: sudo cat $data/token.txt"
echo "No firewall rule was changed. Thermostats need inbound TCP $port allowed, from the home network only."
if [ -n "$copy_from" ]; then
    echo "The readings and token were copied. $copy_from is now an old copy: do not run a server from it."
fi
