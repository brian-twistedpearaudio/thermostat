# Thermostat server

ASP.NET Core (.NET 10) with SQLite. Stores the 5-minute records the thermostats send, returns the history of each, and keeps their configuration so it can be changed from here.

## Run

```powershell
dotnet run
```

It listens on port 5080 on all network addresses (`Urls` in `appsettings.json`). Windows may ask to allow it through the firewall; allow **private networks only**.

The first run makes `data/token.txt`. Enter that token, and this PC's address such as `192.168.1.10:5080`, on the thermostat's Wi-Fi setup page. Give the PC a fixed address (a DHCP reservation in the router). Use its IP address: `.local` names are not looked up.

`data/` holds the readings (`thermostat.db`) and the token, and is not committed.

## Run as a Windows service

The service starts with Windows, restarts if it fails, and runs as the virtual account `NT SERVICE\ThermostatServer`, which has no password and can write only to its data folder and where any ordinary user can. It needs the .NET 10 SDK to build and the ASP.NET Core 10 runtime installed for all users (the SDK installer includes it).

Stop the server in its terminal (Ctrl+C), then in PowerShell opened with **Run as administrator**, in this folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\install-service.ps1 -CopyFrom .\data
```

This copies the readings and the token from `data`, so the thermostats need no change; `data` itself is left as it was. It is then an old copy: do not run a server from it again. For a first install with no readings use `-NewData` instead of `-CopyFrom`. One of them is required the first time, and neither ever overwrites existing data.

| | Folder |
|---|---|
| Program | `C:\Program Files\Thermostat Server\app` |
| Readings and token | `C:\ProgramData\ThermostatServer` |

Only administrators and the service can open the data folder. To read the token, run `Get-Content C:\ProgramData\ThermostatServer\token.txt` as administrator. Copy the folder for a backup only with the service stopped.

The scripts do not change the firewall. Allow inbound TCP 5080 for `Thermostat.Server.exe` in the program folder, on **Private networks only**.

To update after changing the source (the service stops briefly; settings come from this folder's `appsettings.json`, and the scripts expect port 5080):

```powershell
powershell -ExecutionPolicy Bypass -File .\update-service.ps1
```

To uninstall, keeping the data folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\uninstall-service.ps1
```

Warnings and errors go to Event Viewer, **Windows Logs**, **Application**.

## Run on Linux

For a 64-bit PC or VM (Intel or AMD), or a Raspberry Pi with 64-bit Raspberry Pi OS. It needs systemd and glibc, such as Debian, Ubuntu or Fedora have. The host does not need .NET. Publish on any PC with the .NET 10 SDK, `linux-x64` for the PC or VM or `linux-arm64` for the Pi (`uname -m` on the host says `x86_64` or `aarch64`):

```powershell
dotnet publish -c Release -r linux-x64 --self-contained
```

Copy the folder `bin/Release/net10.0/linux-x64/publish` (or `linux-arm64`) to the host, for example with `scp -r`. Copy the whole folder into a new one each time. In it, on the host:

```bash
sudo bash linux/install.sh --copy-from ~/data    # or --new-data to start with no readings
```

One of the options is required the first time, and neither ever overwrites existing data. The service starts with the system, restarts if it fails, and runs as its own user, `thermostat-server`, which cannot log in. The program is in `/opt/thermostat-server/app`; the readings and token are in `/var/lib/thermostat-server`, which only that user and root can open. To read the token: `sudo cat /var/lib/thermostat-server/token.txt`. Back up that folder only with the service stopped.

To update, publish and copy again, then `sudo bash linux/update.sh`. To uninstall, keeping the data and its user, `sudo bash linux/uninstall.sh`. Settings come from the published `appsettings.json`, and the scripts expect port 5080. Logs: `journalctl -u thermostat-server`.

The scripts do not change the firewall. Allow inbound TCP 5080 from the home network only.

On a Raspberry Pi, keep the database on an SSD or a good-quality SD card: it is written to every few minutes.

Not supported: 32-bit systems (32-bit Raspberry Pi OS included), Alpine (musl) and systems without systemd.

### Move from the Windows PC

1. Stop the server on the PC: Ctrl+C in its terminal, or `Stop-Service ThermostatServer` as administrator. Make sure it cannot start again: `Set-Service ThermostatServer -StartupType Disabled`.
2. Copy `thermostat.db`, `token.txt` and, if there is one, `thermostat.db-wal` to a private folder on the host, such as `~/data`. They are in `data`, or for the service in `C:\ProgramData\ThermostatServer` (as administrator).
3. Install with `--copy-from` as above. Give the host a fixed address, and delete the copy in `~/data` when all is well.
4. Enter the new address, such as `192.168.1.20:5080`, on each thermostat's Wi-Fi setup page. The token stays the same. The thermostats keep their records until they reach the new server.

## Charts

Open `http://<this PC's address>:5080` in a browser on the home network (`http://localhost:5080` on this PC) and enter the token. Select up to 8 thermostats and a range; ranges over 2 days show averages. The charts refresh each minute. A remembered token is kept in that browser until **Forget token**.

## API

Every call except `/health` needs the header `X-Api-Token`. Times are UTC Unix seconds.

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/v1/health` | Is the server up |
| POST | `/api/v1/readings` | Records from a thermostat, up to 500. Safe to repeat; bad records are skipped and counted |
| GET | `/api/v1/devices` | Known thermostats |
| GET | `/api/v1/devices/{id}/readings?from=&to=&bucket=` | History, `to` exclusive, last 24 hours by default. `bucket` (seconds, 300 or more) combines records, with minimum and maximum temperature |
| GET | `/api/v1/devices/{id}/graph?from=&to=&bucket=` | Temperature and set point as short text, up to 400 points, for the thermostat's Graph screen |
| POST | `/api/v1/devices/{id}/sync` | A thermostat's regular call: its configuration and state as text, answered with the configuration to use |
| GET | `/api/v1/devices/{id}/config` | Configuration and state of a thermostat |
| PUT | `/api/v1/devices/{id}/config` | Change any of set point, mode, swing, sensor offset, schedule and name |

The last three are described in [`docs/sync.md`](../docs/sync.md).

A thermostat's ID is its Wi-Fi MAC address in hex. A history has at most 10,000 points; `truncated` is true if there were more.

## Security

One shared token over plain HTTP, for a home network only. The token allows changing the heating as well as reading the history: anyone who has it can set the thermostats. Do not open the port to the internet.

## Not yet included

CSV export.
