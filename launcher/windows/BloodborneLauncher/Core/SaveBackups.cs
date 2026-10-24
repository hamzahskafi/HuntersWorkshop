using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace BloodborneLauncher.Core
{
    /// <summary>
    /// Copies of the current profile's savedata, made before each start when it changed since the
    /// last copy; the newest ten are kept, in launcher-backups\&lt;profile&gt;\&lt;time&gt; in the saves
    /// folder. While the game runs, a copy is also made every few minutes when the saves changed
    /// (CreateInGame); those are counted apart, so a long session does not push out the copies
    /// made before starting. Restoring first backs up what is there now.
    /// </summary>
    public static class SaveBackups
    {
        public const int Keep = 10;
        public const int InGameKeep = 5;
        public static readonly TimeSpan InGameEvery = TimeSpan.FromMinutes(15);
        /// <summary>How long the saves must have been left alone before a copy while playing: the
        /// game writes its save files in place, and a copy taken mid-write would be torn.</summary>
        private static readonly TimeSpan InGameQuiet = TimeSpan.FromSeconds(45);
        private const string Stamp = "yyyy-MM-dd_HH-mm-ss";
        private const string BeforeRestore = "_before-restore";
        private const string InGame = "_in-game";
        private const string FingerprintFile = "fingerprint.txt";

        public sealed record Backup(string Dir, DateTime When, long Bytes, bool MadeBeforeRestore, bool MadeInGame = false);

        public static string SaveData(LauncherSettings s) => Path.Combine(GameEnvironment.UserDir(s), "savedata");

        public static string Folder(LauncherSettings s) =>
            Path.Combine(GameEnvironment.SavesRoot(s), "launcher-backups", PlayerProfiles.Current(s));

        /// <summary>This profile's backups, newest first.</summary>
        public static List<Backup> List(LauncherSettings s) => ListIn(Folder(s));

        /// <summary>The backups in a profile's backup folder, newest first. Reads the disk only (each
        /// backup's size too), so it can run on a worker thread.</summary>
        public static List<Backup> ListIn(string folder)
        {
            if (!Directory.Exists(folder)) return new();
            var list = new List<Backup>();
            foreach (var dir in Directory.GetDirectories(folder))
            {
                var name = Path.GetFileName(dir);
                var before = name.EndsWith(BeforeRestore, StringComparison.Ordinal);
                if (before) name = name[..^BeforeRestore.Length];
                var inGame = name.EndsWith(InGame, StringComparison.Ordinal);
                if (inGame) name = name[..^InGame.Length];
                if (DateTime.TryParseExact(name, Stamp, CultureInfo.InvariantCulture, DateTimeStyles.None, out var when))
                    list.Add(new Backup(dir, when, Size(dir), before, inGame));
            }
            return list.OrderByDescending(b => b.When).ToList();
        }

        /// <summary>Copies the savedata unless the newest backup already holds the same files.
        /// Returns the new backup's folder, or null when there was nothing new to copy.</summary>
        public static string? Create(LauncherSettings s, bool beforeRestore = false)
        {
            var source = SaveData(s);
            if (!Directory.Exists(source) || !Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories).Any())
                return null;
            var print = Fingerprint(source);
            if (!beforeRestore && List(s).FirstOrDefault() is { } newest && ReadFingerprint(newest.Dir) == print)
                return null;

            var target = Path.Combine(Folder(s), DateTime.Now.ToString(Stamp, CultureInfo.InvariantCulture) + (beforeRestore ? BeforeRestore : ""));
            if (Directory.Exists(target)) return null; // one a second is plenty
            Copy(source, Path.Combine(target, "savedata"));
            File.WriteAllText(Path.Combine(target, FingerprintFile), print);
            Prune(s);
            return target;
        }

        /// <summary>A copy while the game runs: only when the saves changed since the newest backup
        /// and nothing was written to them for a while, and only if no file changed during the copy
        /// (it is made in a side folder and renamed when complete). Returns the new backup's folder,
        /// or null when there was nothing to copy or the game was saving.</summary>
        public static string? CreateInGame(LauncherSettings s)
        {
            var source = SaveData(s);
            if (!Directory.Exists(source)) return null;
            var files = Directory.GetFiles(source, "*", SearchOption.AllDirectories).Where(f => !Staging(source, f)).ToList();
            if (files.Count == 0) return null;
            if (DateTime.UtcNow - files.Max(File.GetLastWriteTimeUtc) < InGameQuiet) return null;
            var print = Fingerprint(source);
            if (List(s).FirstOrDefault() is { } newest && ReadFingerprint(newest.Dir) == print) return null;

            var target = Path.Combine(Folder(s), DateTime.Now.ToString(Stamp, CultureInfo.InvariantCulture) + InGame);
            if (Directory.Exists(target)) return null;
            // Side folders left by a copy that never finished (the launcher closed during it).
            foreach (var left in Directory.Exists(Folder(s)) ? Directory.GetDirectories(Folder(s), "*.partial") : Array.Empty<string>())
            {
                try { Directory.Delete(left, recursive: true); } catch { /* in use: next time */ }
            }
            var partial = target + ".partial";
            Copy(source, Path.Combine(partial, "savedata"));
            if (Fingerprint(source) != print)
            {
                Directory.Delete(partial, recursive: true); // saved meanwhile: next time
                return null;
            }
            File.WriteAllText(Path.Combine(partial, FingerprintFile), print);
            Directory.Move(partial, target);
            Prune(s);
            return target;
        }

        /// <summary>The newest Keep backups made outside the game and InGameKeep made while it ran.</summary>
        private static void Prune(LauncherSettings s)
        {
            var all = List(s);
            var old = all.Where(b => !b.MadeInGame).Skip(Keep).Concat(all.Where(b => b.MadeInGame).Skip(InGameKeep));
            foreach (var backup in old)
            {
                try { Directory.Delete(backup.Dir, recursive: true); } catch { /* in use: next time */ }
            }
        }

        /// <summary>Puts a backup's savedata back, after backing up the current one.</summary>
        public static void Restore(LauncherSettings s, Backup backup)
        {
            var from = Path.Combine(backup.Dir, "savedata");
            if (!Directory.Exists(from)) throw new DirectoryNotFoundException(L.T("This backup has no savedata folder."));
            Create(s, beforeRestore: true);

            var target = SaveData(s);
            // Copied next to it first, so a failed copy leaves the current saves in place.
            var staging = target + ".restoring";
            if (Directory.Exists(staging)) Directory.Delete(staging, recursive: true);
            Copy(from, staging);
            if (Directory.Exists(target)) Directory.Delete(target, recursive: true);
            Directory.Move(staging, target);
        }

        /// <summary>A save import's staging folder (SaveImport): never part of the saves. One left by
        /// an older import held a whole Downloads folder, copied into every backup.</summary>
        private static bool Staging(string from, string path) =>
            Path.GetRelativePath(from, path).Split(Path.DirectorySeparatorChar)
                .Any(part => part.EndsWith(".importing", StringComparison.OrdinalIgnoreCase));

        internal static void Copy(string from, string to)
        {
            Directory.CreateDirectory(to);
            foreach (var dir in Directory.GetDirectories(from, "*", SearchOption.AllDirectories).Where(d => !Staging(from, d)))
                Directory.CreateDirectory(Path.Combine(to, Path.GetRelativePath(from, dir)));
            foreach (var file in Directory.GetFiles(from, "*", SearchOption.AllDirectories).Where(f => !Staging(from, f)))
                File.Copy(file, Path.Combine(to, Path.GetRelativePath(from, file)), overwrite: true);
        }

        /// <summary>Names, sizes and times of the files: equal when nothing was saved since.</summary>
        internal static string Fingerprint(string dir)
        {
            var text = new StringBuilder();
            foreach (var file in Directory.GetFiles(dir, "*", SearchOption.AllDirectories).Where(f => !Staging(dir, f)).OrderBy(f => f, StringComparer.Ordinal))
            {
                var info = new FileInfo(file);
                text.Append(Path.GetRelativePath(dir, file)).Append('|').Append(info.Length).Append('|')
                    .Append(info.LastWriteTimeUtc.Ticks).Append('\n');
            }
            return Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(text.ToString())));
        }

        private static string? ReadFingerprint(string backup)
        {
            try { return File.ReadAllText(Path.Combine(backup, FingerprintFile)).Trim(); }
            catch { return null; }
        }

        private static long Size(string dir)
        {
            try { return Directory.GetFiles(dir, "*", SearchOption.AllDirectories).Sum(f => new FileInfo(f).Length); }
            catch { return 0; }
        }
    }
}
