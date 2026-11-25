using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace Thermostat.Server;

/// <summary>A field's value and the time it was last changed, in Unix seconds.</summary>
public record Field(string Value, long Changed);

/// <summary>A thermostat's call. The temperatures are in tenths of a degree F.</summary>
public record SyncRequest(
    Dictionary<string, Field> Fields,
    int? TempTenths,
    int SetpointTenths,
    bool Heating,
    bool Manual);

/// <summary>The text of a sync call, and which values are allowed. The contract is docs/sync.md.</summary>
public static partial class ConfigSync
{
    /// <summary>All the fields, in the order they are sent.</summary>
    public static readonly string[] Keys = ["set", "mode", "swing", "offset", "schedule", "name"];

    public static bool IsValid(string key, string value) => key switch
    {
        "set" => IsSetPoint(value),
        "mode" => value is "heat" or "off",
        "swing" => Whole(value) is >= 5 and <= 30 and var n && n % 5 == 0,
        "offset" => Whole(value) is >= -100 and <= 100,
        "schedule" => IsSchedule(value),
        "name" => value.Length <= 16 && value.All(c => c is >= ' ' and <= '~') && value == value.Trim(' '),
        _ => false,
    };

    /// <summary>Null if the text is not exactly as it should be.</summary>
    public static SyncRequest? Parse(string text)
    {
        // The last line ends in \n too, so the last part is empty.
        var lines = text.Split('\n');
        if (lines.Length != Keys.Length + 4 || lines[0] != "sync 1" || lines[^2] != "end" || lines[^1] != "") return null;

        var fields = new Dictionary<string, Field>();
        for (var i = 0; i < Keys.Length; i++)
        {
            var parts = lines[i + 1].Split(' ', 3);
            if (parts.Length != 3 || parts[0] != Keys[i] || Time(parts[1]) is not { } time) return null;
            if (!IsValid(parts[0], parts[2])) return null;
            fields[parts[0]] = new Field(parts[2], time);
        }

        var state = lines[^3].Split(' ');
        if (state.Length != 5 || state[0] != "state") return null;
        var temp = Whole(state[1]);
        if (temp is null && state[1] != "-") return null;
        if (Whole(state[2]) is not { } setpoint) return null;
        if (state[3] is not ("0" or "1") || state[4] is not ("0" or "1")) return null;
        return new SyncRequest(fields, temp, setpoint, state[3] == "1", state[4] == "1");
    }

    public static string Format(IReadOnlyDictionary<string, Field> fields)
    {
        var text = new StringBuilder("sync 1\n");
        foreach (var key in Keys)
        {
            text.Append(key).Append(' ').Append(fields[key].Changed).Append(' ').Append(fields[key].Value).Append('\n');
        }
        return text.Append("end\n").ToString();
    }

    private static bool IsSetPoint(string value) => Whole(value) is >= 45 and <= 85;

    private static bool IsSchedule(string value)
    {
        if (value == "-") return true;
        var entries = value.Split(',');
        if (entries.Length > 6) return false;
        var last = "";
        foreach (var entry in entries)
        {
            if (!ScheduleEntry().IsMatch(entry) || !IsSetPoint(entry[5..])) return false;
            var time = entry[..4];
            if (string.CompareOrdinal(time, last) <= 0) return false;
            last = time;
        }
        return true;
    }

    private static int? Whole(string value) =>
        WholeNumber().IsMatch(value) ? int.Parse(value, CultureInfo.InvariantCulture) : null;

    private static long? Time(string value) =>
        TimeNumber().IsMatch(value) ? long.Parse(value, CultureInfo.InvariantCulture) : null;

    // One way only to write each number: no plus sign, no zeros in front.
    [GeneratedRegex(@"\A(0|-?[1-9][0-9]{0,4})\z")]
    private static partial Regex WholeNumber();

    [GeneratedRegex(@"\A(0|-1|[1-9][0-9]{0,17})\z")]
    private static partial Regex TimeNumber();

    [GeneratedRegex(@"\A([01][0-9]|2[0-3])[0-5][0-9]=[0-9]{2}\z")]
    private static partial Regex ScheduleEntry();
}
