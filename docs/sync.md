# Configuration sync

Thermostats run no HTTP server. Each one calls the server every 10 seconds, and soon after a change on its own screen. The call carries its configuration and state, and the reply is the configuration to use. It is also the check that the server is reachable. While the server is down the thermostat keeps heating on what it has, saved in flash.

Each field has a value and the time it was last changed. **The newest change of each field wins.** Nothing has to be queued: an unanswered call is simply made again.

## Fields

| Key | Value | Allowed |
|---|---|---|
| `set` | set point, whole °F. A change is a manual change: it lasts until the next schedule event, if there is a schedule | 45 to 85 |
| `mode` | `heat` or `off` | |
| `swing` | tenths of a °F | 5 to 30, multiples of 5 |
| `offset` | sensor offset, tenths of a °F | -100 to 100 |
| `schedule` | `HHMM=F` entries, comma-separated, in order of time, such as `0600=68,2200=62`; `-` for none. Local time of the thermostat | up to 6, no time twice, set points as `set` |
| `name` | 0 to 16 printable ASCII characters (0x20 to 0x7E), no space at either end | |

Brightness and dim timeout stay on the thermostat.

A field's time is UTC Unix seconds. `0` means never changed. `-1`, sent only by a thermostat, means changed when its clock was not set: the server takes it as changed now.

## `POST /api/v1/devices/{id}/sync`

Header `X-Api-Token`. Request and reply are `text/plain`, ASCII, lines ending in `\n`:

```
sync 1
set 1795640000 70
mode 0 heat
swing 0 10
offset 1795630000 -5
schedule 0 -
name 1795630000 Kitchen
state 721 700 1 0
end
```

- One line per field, all six, in this order: key, time, value. The name is the rest of the line, so it may hold spaces or be empty (the line then ends with the space after the time).
- `state` is in requests only: temperature in tenths of a °F (or `-` if the sensor is faulty), set point in force in tenths, heating now (1 or 0), manual change in force (1 or 0).
- `end` shows that nothing was cut off.

The server, in one transaction: registers the device if it is new (with its last-seen time); takes as its limit its own time, or one more than the stored time if that is later (so a change is never lost to clocks that disagree); lowers later times to the limit, and turns `-1` into the limit; then for each field stores the sent value if it has none, or if the sent time is later than the stored one. It replies with what it now holds. A request that is not exactly as above gets 400 and changes nothing. Body limit 1 KB.

The thermostat takes each field of the reply, unless that field changed on the thermostat after the request was made. A reply that is not exactly as above, or with any value not allowed, is ignored as a whole. When it changes a field itself, the new time is at least one more than the old.

The device's name shown elsewhere is this `name` once there is one.

## For the web page (JSON, header `X-Api-Token`)

`GET /api/v1/devices/{id}/config`: 404 for an unknown device. `fields` is null if the thermostat has never synced, `state` is null if it has not since the server started:

```json
{
  "fields": {
    "set": { "value": "70", "changed": 1795640000, "applied": true },
    "mode": { "value": "heat", "changed": 0, "applied": true }
  },
  "state": { "time": 1795640005, "age": 4, "tempF": 72.1, "setpointF": 70.0, "heating": true, "manual": false }
}
```

All six fields are present, in the order above. `applied` is true if the thermostat's last call already had this time and value for the field. `age` is seconds since that call, by the server's clock. A name is also in `GET /devices` as now.

`PUT /api/v1/devices/{id}/config`, body `{"set":"71","mode":"off"}`: any of the fields, values as strings in the form above. Valid values are all stored with the time now (at least one more than the stored time), a value equal to the stored one included for `set` (it restarts a manual change) and left alone for the others. The answer is as for GET. Anything not allowed: 400 `{"error":"<field>"}` and nothing is stored. 404 for an unknown device, 409 if the thermostat has never synced. A name change here updates the device's name at once.
