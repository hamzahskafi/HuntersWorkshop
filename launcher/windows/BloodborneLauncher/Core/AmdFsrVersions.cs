using System.IO;

namespace BloodborneLauncher.Core
{
    /// <summary>
    /// The AMD FSR versions AMD's FSR SDK offers on the game's GPU, for Graphics › FSR version (0.4.4 beta
    /// 27c). Asked as the engine asks, through the package's bb-gpu-capabilities --fsr-versions (AMD's own
    /// query on a device of that GPU), once per launcher session in the background; the last answer is
    /// kept in the launcher's settings (amd_fsr_versions), so the list shows at once on the next start.
    /// Empty when it cannot tell (no AMD files, not Windows, the tool failed): Automatic only. Names as
    /// AMD gives them, " *" for one AMD's driver provides; config.ini's fsr_version keeps Key(name).
    /// </summary>
    public static class AmdFsrVersions
    {
        private const string SettingKey = "amd_fsr_versions";
        private static bool _asked;

        /// <summary>The versions known now (the last answer until this session's arrives).</summary>
        public static IReadOnlyList<string> Known { get; private set; } = Remembered();

        /// <summary>Raised on a worker thread when this session's answer arrived and differs; the handler
        /// calls Remember on the UI thread.</summary>
        public static event Action? Changed;

        /// <summary>Keeps the answer for the next start (the launcher's settings; on the UI thread).</summary>
        public static void Remember()
        {
            Workspace.Current.Settings.Set(SettingKey, string.Join("|", Known));
            Workspace.Current.SaveSoon();
        }

        private static IReadOnlyList<string> Remembered()
        {
            try
            {
                var saved = Workspace.Current.Settings.Str(SettingKey);
                return saved.Length == 0 ? Array.Empty<string>() : saved.Split('|', StringSplitOptions.RemoveEmptyEntries);
            }
            catch
            {
                return Array.Empty<string>();
            }
        }

        /// <summary>Asks once per launcher session, in the background (Changed when the answer differs).</summary>
        public static void Refresh()
        {
            if (_asked || !OperatingSystem.IsWindows() || !Paths.AmdFsrPresent) return;
            _asked = true;
            Task.Run(() =>
            {
                var answer = Ask();
                if (answer == null || answer.SequenceEqual(Known)) return;
                Known = answer;
                Changed?.Invoke();
            });
        }

        /// <summary>The tool's answer: "version&lt;tab&gt;name" lines; null when it cannot tell.</summary>
        private static IReadOnlyList<string>? Ask()
        {
            try
            {
                var tool = Path.Combine(Paths.EngineDir, "out", Paths.Program("bb-gpu-capabilities"));
                if (!File.Exists(tool)) return null;
                var info = new System.Diagnostics.ProcessStartInfo(tool, "--fsr-versions")
                {
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };
                info.Environment["BB_AMD_FSR_DIR"] = Path.Combine(Paths.EngineDir, "amd-fsr");
                using var process = System.Diagnostics.Process.Start(info);
                if (process == null) return null;
                var output = process.StandardOutput.ReadToEnd();
                if (!process.WaitForExit(15000) || process.ExitCode != 0) return null;
                var versions = output.Split('\n')
                    .Select(line => line.TrimEnd('\r'))
                    .Where(line => line.StartsWith("version\t", StringComparison.Ordinal))
                    .Select(line => line["version\t".Length..].Trim())
                    .Where(name => name.Length > 0)
                    .ToList();
                return versions.Count > 0 ? versions : null;
            }
            catch
            {
                return null;
            }
        }

        /// <summary>A version as config.ini keeps it (the engine's FsrVersionKey): AMD's " *" and an
        /// "FSR " prefix dropped, lowercase; "auto" for nothing.</summary>
        public static string Key(string name)
        {
            var key = name.Trim().TrimEnd('*').Trim().ToLowerInvariant();
            if (key.StartsWith("fsr", StringComparison.Ordinal)) key = key[3..].Trim();
            return key.Length == 0 ? "auto" : key;
        }

        /// <summary>One AMD's driver provides (" *" in AMD's list).</summary>
        public static bool FromDriver(string name) => name.Contains('*');

        /// <summary>Which of `offered` a wanted version asks for (the engine's FsrVersionMatch): its exact
        /// name, or the first of a family ("3.1"), one shipped with the game before the driver's; -1 for
        /// "auto" or none.</summary>
        public static int Match(IReadOnlyList<string> offered, string wanted)
        {
            var want = Key(wanted);
            if (want == "auto") return -1;
            int found = -1;
            for (var i = 0; i < offered.Count; i++)
            {
                var name = Key(offered[i]);
                var same = name == want || (name.Length > want.Length && name.StartsWith(want, StringComparison.Ordinal) && name[want.Length] == '.');
                if (!same) continue;
                if (found < 0 || (FromDriver(offered[found]) && !FromDriver(offered[i]))) found = i;
            }
            return found;
        }

        /// <summary>The newest by number (the one Automatic runs); null for none.</summary>
        public static string? Newest(IReadOnlyList<string> offered)
        {
            static int[] Numbers(string name) =>
                System.Text.RegularExpressions.Regex.Matches(Key(name), @"\d+").Select(m => int.Parse(m.Value, System.Globalization.CultureInfo.InvariantCulture)).ToArray();
            static int Compare(int[] a, int[] b)
            {
                for (var i = 0; i < Math.Min(a.Length, b.Length); i++)
                    if (a[i] != b[i]) return a[i].CompareTo(b[i]);
                return a.Length.CompareTo(b.Length);
            }
            string? newest = null;
            foreach (var name in offered)
                if (newest == null || Compare(Numbers(name), Numbers(newest)) > 0) newest = name;
            return newest;
        }
    }
}
