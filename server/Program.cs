using System.Collections.Concurrent;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Http.Features;
using Microsoft.Extensions.Hosting.WindowsServices;
using Thermostat.Server;

const int MaxBatch = 500;
const int MaxPoints = 10_000;
const int MaxGraphPoints = 400;
const int MaxSync = 1024;
const long MaxRange = 10L * 366 * 86400;

// A published copy has its files beside the program, whatever folder it is started from.
var published = Directory.Exists(Path.Combine(AppContext.BaseDirectory, "wwwroot"));
var builder = WebApplication.CreateBuilder(new WebApplicationOptions
{
    Args = args,
    ContentRootPath = published ? AppContext.BaseDirectory : null,
});
// Each does nothing unless started as that kind of service.
builder.Services.AddWindowsService(o => o.ServiceName = "ThermostatServer");
builder.Services.AddSystemd();
builder.WebHost.ConfigureKestrel(k => k.Limits.MaxRequestBodySize = 256 * 1024);
var app = builder.Build();

var folder = Path.GetFullPath(app.Configuration["DataFolder"] ?? "data", app.Environment.ContentRootPath);
// The install script makes the service's folder. If it is missing, stop rather than start again empty.
if (!WindowsServiceHelpers.IsWindowsService()) Directory.CreateDirectory(folder);
var store = new ReadingStore(Path.Combine(folder, "thermostat.db"));

// The token is made once and kept in a file; type it into the thermostat's setup page.
var tokenFile = Path.Combine(folder, "token.txt");
if (!File.Exists(tokenFile))
{
    File.WriteAllText(tokenFile, RandomNumberGenerator.GetString("abcdefghjkmnpqrstuvwxyz23456789", 20));
}
var token = File.ReadAllText(tokenFile).Trim();
if (token.Length < 16) throw new InvalidOperationException($"The token in {tokenFile} is too short.");
var tokenHash = SHA256.HashData(Encoding.UTF8.GetBytes(token));
app.Logger.LogInformation("Data folder: {Folder}. The API token is in token.txt there.", folder);

// The chart page. It holds no readings: it asks the API for them with the token typed into it.
app.Use((context, next) =>
{
    var headers = context.Response.Headers;
    headers.ContentSecurityPolicy =
        "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; "
        + "base-uri 'none'; form-action 'self'; frame-ancestors 'none'";
    headers.XContentTypeOptions = "nosniff";
    headers["Referrer-Policy"] = "no-referrer";
    headers.CacheControl = "no-cache";
    return next(context);
});
app.UseDefaultFiles();
app.UseStaticFiles();

app.MapGet("/api/v1/health", () => new { status = "ok" });

var api = app.MapGroup("/api/v1");
api.AddEndpointFilter(async (context, next) =>
{
    var given = context.HttpContext.Request.Headers["X-Api-Token"].ToString();
    var ok = CryptographicOperations.FixedTimeEquals(SHA256.HashData(Encoding.UTF8.GetBytes(given)), tokenHash);
    return ok ? await next(context) : Results.Unauthorized();
});

// Safe to send again: readings already stored are left alone. A bad reading is skipped and
// counted, so it cannot hold up the good ones behind it in a thermostat's cache.
api.MapPost("/readings", (Upload upload) =>
{
    if (!Patterns.DeviceId().IsMatch(upload.Device ?? "")) return Results.BadRequest(new { error = "device" });
    var name = upload.Name ?? "";
    if (name.Length > 64 || name.Any(char.IsControl)) return Results.BadRequest(new { error = "name" });
    var readings = upload.Readings ?? [];
    if (readings.Count > MaxBatch) return Results.BadRequest(new { error = "too many readings" });

    var now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
    var good = readings.Where(r => IsValid(r, now)).Select(r => r!).ToList();
    var added = store.Save(upload.Device!, name, now, good);
    var rejected = readings.Count - good.Count;
    if (rejected > 0) app.Logger.LogWarning("{Device}: rejected {Count} readings", upload.Device, rejected);
    return Results.Ok(new { added, duplicates = good.Count - added, rejected });
});

api.MapGet("/devices", () => store.Devices());

// from and to are Unix seconds (to is exclusive); the default is the last 24 hours.
// bucket, in seconds, combines readings into one point each.
api.MapGet("/devices/{id}/readings", (string id, long? from, long? to, long? bucket) =>
{
    if (!store.HasDevice(id)) return Results.NotFound();
    var end = to ?? DateTimeOffset.UtcNow.ToUnixTimeSeconds() + 1;
    if (end is < 0 or > 10_000_000_000) return Results.BadRequest(new { error = "to" });
    var start = from ?? end - 86400;
    if (start < 0 || start >= end || end - start > MaxRange) return Results.BadRequest(new { error = "from" });
    if (bucket is < 300 or > MaxRange) return Results.BadRequest(new { error = "bucket" });

    // One more than the limit shows whether anything was left out.
    if (bucket is { } seconds)
    {
        var buckets = store.Buckets(id, start, end, seconds, MaxPoints + 1);
        return Results.Ok(new { truncated = buckets.Count > MaxPoints, readings = buckets.Take(MaxPoints) });
    }
    var rows = store.Readings(id, start, end, MaxPoints + 1);
    return Results.Ok(new { truncated = rows.Count > MaxPoints, readings = rows.Take(MaxPoints) });
});

// The same history for the thermostat's own screen, as short text: "graph 1", then one line per
// point, "time,avg,min,max,set" in tenths of a degree F; no temperature is left empty.
api.MapGet("/devices/{id}/graph", (string id, long from, long to, long bucket) =>
{
    if (!store.HasDevice(id)) return Results.NotFound();
    if (bucket is < 300 or > MaxRange) return Results.BadRequest(new { error = "bucket" });
    if (from < 0 || from >= to || to > 10_000_000_000 || (to - from) / bucket > MaxGraphPoints)
    {
        return Results.BadRequest(new { error = "range" });
    }
    var text = new StringBuilder("graph 1\n");
    foreach (var b in store.Buckets(id, from, to, bucket, MaxGraphPoints + 1))
    {
        text.Append(b.Time).Append(',').Append(Tenths(b.TempF)).Append(',').Append(Tenths(b.TempMinF)).Append(',')
            .Append(Tenths(b.TempMaxF)).Append(',').Append(Tenths(b.SetpointF)).Append('\n');
    }
    return Results.Text(text.ToString());
});

// Each thermostat's last call, kept in memory only.
var lastSync = new ConcurrentDictionary<string, LastSync>();

// docs/sync.md is the contract for these three. The bodies are read here, after the token check.
api.MapPost("/devices/{id}/sync", async (string id, HttpContext context) =>
{
    context.Features.Get<IHttpMaxRequestBodySizeFeature>()!.MaxRequestBodySize = MaxSync;
    using var body = new MemoryStream();
    try
    {
        await context.Request.Body.CopyToAsync(body);
    }
    catch (BadHttpRequestException e)
    {
        return Results.StatusCode(e.StatusCode);
    }
    if (!Patterns.DeviceId().IsMatch(id)) return Results.BadRequest(new { error = "device" });
    // Latin1 keeps each byte as it is; anything not ASCII is then found in the check.
    var request = ConfigSync.Parse(Encoding.Latin1.GetString(body.GetBuffer(), 0, (int)body.Length));
    if (request is null) return Results.BadRequest(new { error = "sync" });

    var now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
    var fields = store.Merge(id, now, request.Fields);
    lastSync[id] = new LastSync(now, request);
    return Results.Text(ConfigSync.Format(fields));
});

api.MapGet("/devices/{id}/config", (string id) => store.HasDevice(id) ? Config(id) : Results.NotFound());

api.MapPut("/devices/{id}/config", async (string id, HttpContext context) =>
{
    if (!store.HasDevice(id)) return Results.NotFound();
    var values = new Dictionary<string, string>();
    var at = "body";
    try
    {
        using var json = await JsonDocument.ParseAsync(context.Request.Body);
        if (json.RootElement.ValueKind != JsonValueKind.Object) return Results.BadRequest(new { error = at });
        foreach (var property in json.RootElement.EnumerateObject())
        {
            at = "body";
            at = property.Name;
            var value = property.Value.ValueKind == JsonValueKind.String ? property.Value.GetString()! : null;
            if (value is null || !ConfigSync.IsValid(at, value) || !values.TryAdd(at, value))
            {
                return Results.BadRequest(new { error = at });
            }
        }
    }
    // InvalidOperationException: text that is not valid Unicode.
    catch (Exception e) when (e is JsonException or InvalidOperationException)
    {
        return Results.BadRequest(new { error = at });
    }

    var now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
    return store.Change(id, now, values) ? Config(id) : Results.Conflict();
});

app.Run();

IResult Config(string id)
{
    var last = lastSync.GetValueOrDefault(id);
    var fields = store.Config(id);
    // A dictionary keeps the order the fields are added in.
    var shown = fields.Count == 0 ? null : ConfigSync.Keys.ToDictionary(key => key, key => new
    {
        fields[key].Value,
        fields[key].Changed,
        Applied = last is not null && last.Request.Fields[key] == fields[key],
    });
    var state = last is null ? null : new
    {
        last.Time,
        Age = Math.Max(0, DateTimeOffset.UtcNow.ToUnixTimeSeconds() - last.Time),
        TempF = last.Request.TempTenths / 10.0,
        SetpointF = last.Request.SetpointTenths / 10.0,
        last.Request.Heating,
        last.Request.Manual,
    };
    return Results.Ok(new { fields = shown, state });
}

static int? Tenths(double? v) => v is { } x ? (int)Math.Round(x * 10) : null;

static bool IsValid(Reading? r, long now) =>
    r is not null
    && r.Time > 1_700_000_000 && r.Time <= now + 86400 && r.Time % 300 == 0
    && r.TempF is null or (>= -40 and <= 150)
    && r.Humidity is null or (>= 0 and <= 100)
    && r.PressureHpa is null or (>= 300 and <= 1200)
    && r.SetpointF is >= 0 and <= 120
    && r.HeatPct is >= 0 and <= 100;

public record Upload(string? Device, string? Name, List<Reading?>? Readings);

public record LastSync(long Time, SyncRequest Request);

public static partial class Patterns
{
    [GeneratedRegex(@"\A[0-9a-f]{12}\z")]
    public static partial Regex DeviceId();
}
