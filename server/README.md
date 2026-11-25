# Thermostat server

ASP.NET Core (.NET 10) with SQLite. Stores the 5-minute records the thermostats send and returns the history of each.

## Run

```powershell
dotnet run
```

It listens on port 5080 on all network addresses (`Urls` in `appsettings.json`). Windows may ask to allow it through the firewall; allow **private networks only**.

The first run makes `data/token.txt`. Enter that token, and this PC's address such as `192.168.1.10:5080`, on the thermostat's Wi-Fi setup page. Give the PC a fixed address (a DHCP reservation in the router). Use its IP address: `.local` names are not looked up.

`data/` holds the readings (`thermostat.db`) and the token, and is not committed.

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

A thermostat's ID is its Wi-Fi MAC address in hex. A history has at most 10,000 points; `truncated` is true if there were more.

## Security

One shared token over plain HTTP, for a home network only. Do not open the port to the internet.

## Not yet included

CSV export, control of the thermostat, running as a Windows service.
