using System.IO;

namespace BloodborneLauncher.Core
{
    /// <summary>
    /// config.ini: the port's own settings (upscaler, preset, controls, ...), also edited by the
    /// in-game menu. The edited keys are rewritten in place, missing ones
    /// appended, comments and unknown keys kept, and a key set to null is removed (a control
    /// binding back to its default).
    /// </summary>
    public sealed class IniFile
    {
        /// <summary>The keys a new config.ini gets, at the engine's own defaults (bbport_settings.h).
        /// 0.4.4 beta 26: the upscaler and preset as the engine's, FSR at Native AA, which run on any GPU
        /// (were FSR 4 at Ultra Performance); the first start's Best settings for this PC still offers DLSS
        /// and a preset by GPU. A config.ini that has them keeps its own. 0.4.4 beta 27: "fsr", AMD's FSR.
        /// 27c: the quality and DLSS's model "auto", the FSR version AMD's choice. engine/config.default.ini
        /// and the engine's defaults agree (tests/test_engine_defaults.py).</summary>
        public static readonly (string Key, string Value)[] Defaults =
        {
            ("upscaler", "off"), // 0.4.4 beta 28 (the tester): the game's own AA by default
            ("fsr_version", "auto"),
            ("preset", "auto"),
            ("sharpen", "0"), // 0.4.6 beta 16 (the tester): off by default; Load switches the old default off once
            ("sharpening", "25"), // 0.4.6 beta 16 (the tester): a percentage of RCAS's maximum (was sharpness)
            ("object_motion", "1"),
            ("show_fps", "0"),
            ("compile_note", "1"),
            ("output_res", "1920x1080"),
            ("model_lod", "0"),
            ("camera_tuning", "original"),
            ("live_resolution", "0"),
            ("dlss_preset", "auto"),
            ("fps_position", "0"),
            ("fps_scale", "1.00"),
            ("fps_text_color", "#FFFFFF"),
            ("fps_bg_color", "#0F0F0F"),
            ("fps_bg_opacity", "0.50"),
            ("fps_text_opacity", "1.00"),
            ("fps_border", "1"),
        };

        private readonly Dictionary<string, string?> _values = new(StringComparer.Ordinal);
        private List<string> _lines = new();
        /// <summary>Keys the launcher changed since the file was read: only these are written over it.
        /// The game edits the same file (in-game menu) while the launcher holds an older copy.</summary>
        private readonly HashSet<string> _changed = new(StringComparer.Ordinal);

        /// <summary>config.ini's time when it was last read or written here: a later time on disk means
        /// the game (or someone) changed it since (MainWindow reads it again).</summary>
        public DateTime FileTime { get; private set; }

        /// <summary>Changes made here that are not in the file yet (Workspace holds them while the game runs).</summary>
        public bool Pending => _changed.Count > 0;

        public void Load()
        {
            _changed.Clear();
            try { FileTime = File.Exists(Paths.IniPath) ? File.GetLastWriteTimeUtc(Paths.IniPath) : default; }
            catch (IOException) { FileTime = default; }
            _values.Clear();
            foreach (var (key, value) in Defaults)
                _values[key] = value;
            foreach (var effect in Choices.Effects)
                _values[effect.Key] = effect.Default ? "1" : "0";

            _lines = new List<string>();
            try
            {
                if (File.Exists(Paths.IniPath))
                    _lines = File.ReadAllLines(Paths.IniPath).ToList();
            }
            catch
            {
                // Unreadable: start from the defaults.
            }

            var fileKeys = new HashSet<string>(StringComparer.Ordinal);
            foreach (var line in _lines)
            {
                if (!line.Contains('=') || line.TrimStart().StartsWith('#'))
                    continue;
                var at = line.IndexOf('=');
                var key = line[..at].Trim();
                fileKeys.Add(key);
                _values[key] = line[(at + 1)..].Trim();
            }
            // 0.4.4 beta 27: FSR 3.1, FSR 4 and FSR 4.1.1 are AMD's FSR ("fsr"), as the engine reads them;
            // shown as it, the file left as it is until something is saved.
            if (_values.TryGetValue("upscaler", out var upscaler) && upscaler is "fsr3" or "fsr4" or "fsr411")
                _values["upscaler"] = "fsr";
            // 0.4.6 beta 16 (the tester): the strength is a percentage of RCAS's maximum (`sharpening`, 25
            // by default) in place of AMD's sharpness (`sharpness`): an old value kept as the same
            // strength, the old default (0.20) made the new one, the old line removed at the next save;
            // as the engine does (bbport_settings.h, SharpeningFromOld).
            double? oldSharpness = null;
            if (_values.TryGetValue("sharpness", out var oldText))
            {
                if (double.TryParse(oldText, System.Globalization.NumberStyles.Float,
                        System.Globalization.CultureInfo.InvariantCulture, out var old))
                    oldSharpness = old;
                if (!fileKeys.Contains("sharpening") && oldSharpness is double s)
                {
                    _values["sharpening"] = SharpeningFromOld(s).ToString(System.Globalization.CultureInfo.InvariantCulture);
                    _changed.Add("sharpening");
                }
                _values["sharpness"] = null;
                _changed.Add("sharpness");
            }
            // 0.4.6 beta 16 (the tester): sharpening off by default. A config.ini with the old default
            // untouched (on, at 0.20) has it switched off once, any other choice kept; the marker says
            // it happened, so turning it on again later stays. The engine does the same when it reads a
            // file without the marker (bbport_settings.cpp, kSharpenOffMarker), and writes the marker.
            if (!_values.ContainsKey(SharpenOffMarker))
            {
                var oldDefault = !fileKeys.Contains("sharpening") &&
                                 (oldSharpness is null || Math.Abs(oldSharpness.Value - 0.2) < 0.001);
                if (_values.GetValueOrDefault("sharpen") == "1" && oldDefault)
                {
                    _values["sharpen"] = "0";
                    _changed.Add("sharpen");
                }
                _values[SharpenOffMarker] = "1";
                _changed.Add(SharpenOffMarker);
            }
        }

        /// <summary>0.4.6 beta 16: the percentage for an old `sharpness` (bbport_settings.h,
        /// SharpeningFromOld): 0.20, the old default, the new default 25; another value the strength the
        /// first beta 16 build gave it (2 - 2s stops below RCAS's maximum); 1 and above 100, 0 or less 0.</summary>
        public static int SharpeningFromOld(double s) =>
            !(s > 0) ? 0 : s > 0.199 && s < 0.201 ? 25 : s >= 1 ? 100 : (int)(100 * Math.Pow(2, 2 * s - 2) + 0.5);

        /// <summary>0.4.6 beta 16: config.ini's sign that the old sharpening default was switched off.</summary>
        public const string SharpenOffMarker = "sharpen_off_046";

        public void Save()
        {
            // What the game wrote meanwhile stays: the file is read again and only the launcher's own
            // changes go over it (PC Enhancements set in the game came back off after it exited).
            var mine = _changed.ToDictionary(k => k, k => _values.TryGetValue(k, out var v) ? v : null, StringComparer.Ordinal);
            Load();
            foreach (var (key, value) in mine)
                _values[key] = value;
            var written = new HashSet<string>(StringComparer.Ordinal);
            var output = new List<string>();
            foreach (var line in _lines)
            {
                if (line.Contains('=') && !line.TrimStart().StartsWith('#'))
                {
                    var key = line[..line.IndexOf('=')].Trim();
                    if (_values.TryGetValue(key, out var value))
                    {
                        if (value != null && written.Add(key))
                            output.Add($"{key}={value}");
                        written.Add(key);
                        continue;
                    }
                }
                output.Add(line);
            }

            if (_lines.Count == 0)
                output.Add("# Hunter's Workshop settings (in-game menu: F1 / L3+R3)");
            foreach (var (key, value) in _values)
                if (!written.Contains(key) && value != null)
                    output.Add($"{key}={value}");

            Directory.CreateDirectory(Path.GetDirectoryName(Paths.IniPath)!);
            File.WriteAllText(Paths.IniPath, string.Join("\n", output) + "\n");
            FileTime = File.GetLastWriteTimeUtc(Paths.IniPath);
            _lines = output;
            // Everything is in the file now, Load's own changes as well (0.4.6 beta 16's marker).
            _changed.Clear();
        }

        /// <summary>The value, or null when the key is absent (e.g. a binding left at its default).</summary>
        public string? Raw(string key) => _values.TryGetValue(key, out var v) ? v : null;

        public string Get(string key, string fallback = "") => Raw(key) ?? fallback;

        public bool Flag(string key) => Get(key) == "1";

        public void Set(string key, string? value)
        {
            if (_values.TryGetValue(key, out var old) && old == value) return;
            _values[key] = value;
            _changed.Add(key);
        }

        /// <summary>Every key with a value (shared settings).</summary>
        public IEnumerable<KeyValuePair<string, string>> Values =>
            _values.Where(kv => kv.Value != null).Select(kv => new KeyValuePair<string, string>(kv.Key, kv.Value!));
    }
}
