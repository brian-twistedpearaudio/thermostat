using Microsoft.Data.Sqlite;

namespace Thermostat.Server;

/// <summary>One 5-minute record. Time is the UTC start of the interval, in Unix seconds.</summary>
public record Reading(
    long Time,
    double? TempF,
    double? Humidity,
    double? PressureHpa,
    double SetpointF,
    int HeatPct,
    bool SensorFault,
    bool ModeOff,
    bool Partial);

public record Device(string Id, string Name, long LastSeen, long? LastReading);

/// <summary>Readings combined into one point of a graph. Empty buckets are not returned.</summary>
public record Bucket(
    long Time,
    int Count,
    double? TempF,
    double? TempMinF,
    double? TempMaxF,
    double? Humidity,
    double? PressureHpa,
    double SetpointF,
    double HeatPct);

/// <summary>SQLite storage. A connection per call: SQLite pools them.</summary>
public class ReadingStore
{
    private readonly string _connectionString;

    public ReadingStore(string path)
    {
        _connectionString = new SqliteConnectionStringBuilder { DataSource = path }.ToString();
        using var db = Open();
        using var cmd = db.CreateCommand();
        cmd.CommandText = """
            PRAGMA journal_mode = WAL;
            CREATE TABLE IF NOT EXISTS devices (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                last_seen INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS readings (
                device TEXT NOT NULL,
                time INTEGER NOT NULL,
                temp_f REAL,
                humidity REAL,
                pressure_hpa REAL,
                setpoint_f REAL NOT NULL,
                heat_pct INTEGER NOT NULL,
                sensor_fault INTEGER NOT NULL,
                mode_off INTEGER NOT NULL,
                partial INTEGER NOT NULL,
                PRIMARY KEY (device, time)
            ) WITHOUT ROWID;
            """;
        cmd.ExecuteNonQuery();
    }

    private SqliteConnection Open()
    {
        var db = new SqliteConnection(_connectionString);
        db.Open();
        return db;
    }

    /// <summary>Saves all or nothing. A reading already stored is left as it is.</summary>
    public int Save(string device, string name, long now, IReadOnlyList<Reading> readings)
    {
        using var db = Open();
        using var tx = db.BeginTransaction();

        using (var cmd = db.CreateCommand())
        {
            cmd.Transaction = tx;
            cmd.CommandText = """
                INSERT INTO devices (id, name, last_seen) VALUES ($id, $name, $now)
                ON CONFLICT (id) DO UPDATE SET name = excluded.name, last_seen = excluded.last_seen
                """;
            cmd.Parameters.AddWithValue("$id", device);
            cmd.Parameters.AddWithValue("$name", name);
            cmd.Parameters.AddWithValue("$now", now);
            cmd.ExecuteNonQuery();
        }

        var added = 0;
        foreach (var r in readings)
        {
            using var cmd = db.CreateCommand();
            cmd.Transaction = tx;
            cmd.CommandText = """
                INSERT INTO readings VALUES
                    ($device, $time, $temp, $hum, $press, $set, $heat, $fault, $off, $partial)
                ON CONFLICT (device, time) DO NOTHING
                """;
            cmd.Parameters.AddWithValue("$device", device);
            cmd.Parameters.AddWithValue("$time", r.Time);
            cmd.Parameters.AddWithValue("$temp", (object?)r.TempF ?? DBNull.Value);
            cmd.Parameters.AddWithValue("$hum", (object?)r.Humidity ?? DBNull.Value);
            cmd.Parameters.AddWithValue("$press", (object?)r.PressureHpa ?? DBNull.Value);
            cmd.Parameters.AddWithValue("$set", r.SetpointF);
            cmd.Parameters.AddWithValue("$heat", r.HeatPct);
            cmd.Parameters.AddWithValue("$fault", r.SensorFault);
            cmd.Parameters.AddWithValue("$off", r.ModeOff);
            cmd.Parameters.AddWithValue("$partial", r.Partial);
            added += cmd.ExecuteNonQuery();
        }

        tx.Commit();
        return added;
    }

    public List<Device> Devices()
    {
        using var db = Open();
        using var cmd = db.CreateCommand();
        cmd.CommandText = """
            SELECT id, name, last_seen, (SELECT MAX(time) FROM readings WHERE device = devices.id)
            FROM devices ORDER BY name, id
            """;
        using var row = cmd.ExecuteReader();
        var list = new List<Device>();
        while (row.Read())
        {
            list.Add(new Device(row.GetString(0), row.GetString(1), row.GetInt64(2),
                row.IsDBNull(3) ? null : row.GetInt64(3)));
        }
        return list;
    }

    public bool HasDevice(string device)
    {
        using var db = Open();
        using var cmd = db.CreateCommand();
        cmd.CommandText = "SELECT COUNT(*) FROM devices WHERE id = $id";
        cmd.Parameters.AddWithValue("$id", device);
        return (long)cmd.ExecuteScalar()! > 0;
    }

    /// <summary>Oldest first, from inclusive, to exclusive.</summary>
    public List<Reading> Readings(string device, long from, long to, int limit)
    {
        using var db = Open();
        using var cmd = db.CreateCommand();
        cmd.CommandText = """
            SELECT time, temp_f, humidity, pressure_hpa, setpoint_f, heat_pct, sensor_fault, mode_off, partial
            FROM readings WHERE device = $device AND time >= $from AND time < $to
            ORDER BY time LIMIT $limit
            """;
        AddRange(cmd, device, from, to, limit);
        using var row = cmd.ExecuteReader();
        var list = new List<Reading>();
        while (row.Read())
        {
            list.Add(new Reading(row.GetInt64(0), Number(row, 1), Number(row, 2), Number(row, 3),
                row.GetDouble(4), row.GetInt32(5), row.GetBoolean(6), row.GetBoolean(7), row.GetBoolean(8)));
        }
        return list;
    }

    public List<Bucket> Buckets(string device, long from, long to, long seconds, int limit)
    {
        using var db = Open();
        using var cmd = db.CreateCommand();
        cmd.CommandText = """
            SELECT time / $bucket * $bucket AS t, COUNT(*), AVG(temp_f), MIN(temp_f), MAX(temp_f),
                   AVG(humidity), AVG(pressure_hpa), AVG(setpoint_f), AVG(heat_pct)
            FROM readings WHERE device = $device AND time >= $from AND time < $to
            GROUP BY t ORDER BY t LIMIT $limit
            """;
        AddRange(cmd, device, from, to, limit);
        cmd.Parameters.AddWithValue("$bucket", seconds);
        using var row = cmd.ExecuteReader();
        var list = new List<Bucket>();
        while (row.Read())
        {
            list.Add(new Bucket(row.GetInt64(0), row.GetInt32(1), Number(row, 2), Number(row, 3), Number(row, 4),
                Number(row, 5), Number(row, 6), row.GetDouble(7), row.GetDouble(8)));
        }
        return list;
    }

    private static void AddRange(SqliteCommand cmd, string device, long from, long to, int limit)
    {
        cmd.Parameters.AddWithValue("$device", device);
        cmd.Parameters.AddWithValue("$from", from);
        cmd.Parameters.AddWithValue("$to", to);
        cmd.Parameters.AddWithValue("$limit", limit);
    }

    private static double? Number(SqliteDataReader row, int i) => row.IsDBNull(i) ? null : row.GetDouble(i);
}
