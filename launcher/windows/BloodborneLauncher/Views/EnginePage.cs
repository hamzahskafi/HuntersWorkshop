using System.Windows;
using System.Windows.Controls;
using BloodborneLauncher.Core;
using Wpf.Ui.Controls;
using TextBlock = System.Windows.Controls.TextBlock;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// The engine preset, its features and the engine's switches (Core/EngineSwitches.cs), so nobody has
    /// to type BB_ variables; a switch left at its default is not passed to the game at all. 0.4.4 beta
    /// 26: Settings' Performance rows and Extra variables are here too, the sound rows in a Sound
    /// group; texture filtering and the motion blur switch are on Picture, the preferred gamepad on
    /// Controls. Reset all covers every row on this page but Extra variables. Every row goes in with
    /// Add, so the settings search finds it (by title, description or variable name).
    /// </summary>
    public sealed class EnginePage : LauncherPage
    {
        /// <summary>Engine switches another page shows, beside what they are about.</summary>
        public static readonly string[] Elsewhere = { "BB_ANISO", "BB_UPSCALER_NO_BLUR", "BB_GAMEPAD" };

        private readonly List<Action> _resets = new();
        /// <summary>This page's launcher settings (not env: switches), for Changed switches and Reset all.</summary>
        private readonly List<(string Key, string Title)> _tracked = new();
        private readonly List<EngineSwitch> _switches = new();
        private readonly TextBlock _changedHint;

        public EnginePage()
        {
            var intro = Text(L.T("Each switch stays at the engine's default until you change it. Only changed ones are used."), "Faint");
            intro.ToolTip = L.T("Only the switches you change are passed to the game, when it starts; a start in safe mode (after a crash) ") +
                            L.T("leaves them out. Extra variables, below, takes anything not listed, and wins over these.");
            Body.Children.Add(intro);
            Add(Card(SymbolRegular.ArrowReset24, L.T("Changed switches"), L.T("Every switch on this page that is not at its default"),
                SmallButton(L.T("Reset all"), SymbolRegular.ArrowUndo24, (_, _) => ResetAll()), out _changedHint));
            // From Settings › Diagnostics (0.4.4 beta 26): beside the switches it adds to and wins over.
            TextRow(SymbolRegular.Code24, L.T("Extra variables"),
                L.T("NAME=value pairs separated by spaces, passed to the game. For anything this page doesn't list; a variable set here wins over its switch"),
                "BB_EXAMPLE=1", () => S.Str("extra_env"), v =>
                {
                    S.Set("extra_env", v.Trim());
                    ShowChanged();
                }, width: 300, line: L.T("NAME=value pairs passed to the game"));

            // Engine features from 0.9.4, each with a switch of its own (were on the Graphics page).
            Group(L.T("Engine features"));
            // 0.4.4 beta 28 (the tester): the Engine preset (New / Legacy 0.9.33) went; each feature has
            // its own switch, and safe mode turns them all off for a run.
            var exp = Advanced(L.T("Experimental engine features"));
            Tracked(SymbolRegular.Code24, L.T("Legacy shader compiler"),
                L.T("Off by default: the native compiler reads constant data through the GPU's constant cache. On compiles the game's shaders the way earlier versions did: try it if something draws wrong. Switching recompiles every shader once"),
                "legacy_shaders", exp, line: L.T("Compiles shaders the old way, if something draws wrong"));
            Tracked(SymbolRegular.Broom24, L.T("VRAM cleanup"),
                L.T("Frees video memory held by textures the GPU drew, so long sessions don't fill up VRAM and slow down. Recommended: on"),
                "exp_vram_cleanup", exp, line: L.T("Frees VRAM held by textures the GPU drew"));
            Tracked(SymbolRegular.Timer24, L.T("Precise frame pacing"),
                L.T("A high-resolution timer and even spacing between frames: steadier frame times and less VRR flicker. Uses a little CPU. On by default"),
                "exp_frame_pacing", exp, line: L.T("Steadier frame times and less VRR flicker"));
            Tracked(SymbolRegular.Desktop24, L.T("Fixed-refresh frame lock"),
                L.T("Only for monitors without G-Sync or FreeSync (VRR). Each frame is shown right after one of the monitor's refreshes when the FPS limit divides its refresh rate (60 FPS on 60, 120 or 240 Hz). Needs Precise frame pacing. With G-Sync or FreeSync it stutters: leave it off. Off by default"),
                "exp_vblank_lock", exp);
            Tracked(SymbolRegular.Shield24, L.T("Particle work safety cap"),
                L.T("Checks the size of the game's GPU particle work before the graphics card runs it, and caps it to what the card supports. Only if the game crashes with \"device lost\" in effect-heavy scenes. It costs smoothness in busy scenes. Off by default"),
                "exp_particle_cap", exp, line: L.T("For \"device lost\" crashes in effect-heavy scenes"));
            Feature(SymbolRegular.Storage24, L.T("Use more VRAM for textures"),
                L.T("Keeps textures in video memory up to 80% of the card instead of about 40%, so they aren't thrown out and loaded again (the cause of most stutters on 8 GB cards)"),
                "exp_vram_budget", exp, line: L.T("Up to 80% of VRAM for textures, for less stutter"));
            Tracked(SymbolRegular.TopSpeed24, L.T("Graphics threads first"),
                L.T("Runs the engine's graphics threads at a higher CPU priority than the game's other threads, so a frame isn't held up behind them. On by default"),
                "exp_thread_priority", exp, line: L.T("Graphics threads at a higher CPU priority"));
            Tracked(SymbolRegular.Sleep24, L.T("Shorter sleep spin"),
                L.T("The game's many short sleeps busy-wait only as long as the timer needs (about 0.2–0.4 ms instead of 0.6 ms), leaving that CPU time to the graphics threads. On by default"),
                "exp_sleep_spin", exp, line: L.T("Leaves more CPU time to the graphics threads"));
            Tracked(SymbolRegular.FlashFlow24, L.T("Background shader compiling"),
                L.T("New shaders compile on helper threads instead of freezing the frame: much shorter hitches when you reach a new area. On by default"),
                "exp_async_pipelines", exp, line: L.T("Much shorter hitches when you reach a new area"));
            Tracked(SymbolRegular.EyeOff24, L.T("Occlusion culling"),
                L.T("Off by default. The game skips objects hidden behind walls: higher FPS in dense areas, but objects and floors can appear late or flicker"),
                "exp_occlusion", exp, line: L.T("Skips objects hidden behind walls: more FPS, some pop-in"));
            Feature(SymbolRegular.FastForward24, L.T("Faster draw thread"),
                L.T("Remembers texture and sampler lookups and hands the end of each frame to the draw thread: higher FPS in busy areas. New: turn it off if the game hangs or crashes"),
                "exp_fast_draws", exp, line: L.T("Higher FPS in busy areas"));
            Feature(SymbolRegular.Shapes24, L.T("Reduce shader stalls"),
                L.T("While a new shader compiles, the scene objects that need it are left out for up to a second instead of the game waiting, " +
                    "and the draw thread goes on with the rest of the frame. Shadow maps, half-resolution effects, post-processing and the UI " +
                    "still wait for it, so some waits remain. New: turn it off if something stays missing"),
                "exp_compile_skip", exp, line: L.T("Some objects appear late while new shaders compile"));
            Tracked(SymbolRegular.ArrowForward24, L.T("Fast loading"),
                L.T("Above 60 FPS, loading screens run at 60 so more of the CPU goes to the loading itself. Does nothing at 30 or 60 FPS, or before a cutscene. On by default"),
                "exp_fast_loading", exp, line: L.T("Faster loading screens above 60 FPS"));
            Tracked(SymbolRegular.Timer24, L.T("Frame rate corrections"),
                L.T("At 60 FPS and above, ragdolls, physics, cloth, jumps, slopes, sprinting, attack sounds, online movement, timers and the follow " +
                    "camera follow the real frame time: the frame's own, the same for the game and every correction, so after a stutter they catch up " +
                    "at once. The timings the FPS++ patches halved or doubled (menu repeat, hit lifetimes, online timers) are the game's own again. " +
                    "Off: the older FPS++ patches above 60. On by default"),
                "exp_frame_fixes", exp, line: L.T("Physics, cloth, timers and camera correct at 60 FPS and up"));
            Tracked(SymbolRegular.Flash24, L.T("Fewer memory write checks"),
                L.T("Memory the game rewrites before almost every use is no longer watched for changes, saving up to about 2 ms a frame in busy areas. On by default"),
                "exp_hot_pages", exp, line: L.T("Up to about 2 ms less a frame in busy areas"));
            Tracked(SymbolRegular.Flow20, L.T("Per-frame GPU data in place"),
                L.T("The memory the game rewrites for the GPU every frame (288 MiB) is read by the GPU where the game writes it, as a PC game's upload heap: no write faults or uploads for it, and less CPU and GPU time a frame. On by default. Off: watched and copied as before 0.4.4, should something look wrong"),
                "exp_gx_in_place", exp, line: L.T("Less CPU and GPU time a frame"));
            Tracked(SymbolRegular.Code24, L.T("Native draw calls"),
                L.T("The game's draw calls reach the engine directly instead of as console GPU commands it decodes again. On by default; turn it off if something stops drawing"),
                "exp_native_calls", exp, line: L.T("Draw calls reach the engine directly"));
            Tracked(SymbolRegular.ShieldCheckmark24, L.T("Effect trail crash fix"),
                L.T("For crashes while drawing effect trails on newer Intel CPUs (Witch of Hemwick, for example): stops the crash, and that kind of trail is not drawn. Off by default"),
                "exp_sfx_workaround", exp, line: L.T("For effect trail crashes on newer Intel CPUs"));

            // The engine's own performance and sound settings in one place each (0.4.4 beta 26: the graphics
            // card from the Graphics group, whose other two rows went to Picture; two-stage pipeline, CPU
            // reads, sound output and sound threads from Settings › Performance).
            Group(L.T(EngineSwitches.Performance));
            Switch("BB_GPU_ID");
            TrackedCombo(SymbolRegular.Flow20, L.T("Two-stage GPU pipeline"), L.T("20–30% faster; turn off if the game is unstable"),
                Choices.DrawPipe, "draw_pipe");
            TrackedCombo(SymbolRegular.ArrowSwap24, L.T("CPU reads of GPU data"),
                L.T("Off: never. Relaxed (default): before the game changes that memory. Precise: before it reads it too; slower"), Choices.Readbacks, "readbacks",
                line: L.T("When the game gets back data the GPU wrote"));
            Switch("BB_PIPELINE_THREADS");
            Switch("BB_FILE_PREFETCH");
            Switch("BB_BACKGROUND_FPS");

            Group(L.T(EngineSwitches.Sound));
            TrackedCombo(SymbolRegular.Speaker224, L.T("Sound output"),
                L.T("If the sound jitters or crackles, try DirectSound. The larger buffer is steadier but the sound comes a little later"),
                Choices.SoundOutput, "sound_output");
            Tracked(SymbolRegular.Speaker224, L.T("Sound threads first"),
                L.T("For CPUs with many slower cores (older Xeons and similar): the game's sound runs before its other threads, against late or stuttering sound. Off by default"),
                "audio_priority", line: L.T("For late or stuttering sound on many-core CPUs"));
            Switch("BB_AUDIO");

            // The rest by their group (Game fixes; any switch added later shows up here too).
            var placed = _switches.Select(x => x.Env).Concat(Elsewhere).ToHashSet();
            foreach (var group in EngineSwitches.Groups.Concat(EngineSwitches.All.Select(x => x.Group)).Distinct())
            {
                var rest = EngineSwitches.All.Where(x => x.Group == group && !placed.Contains(x.Env)).ToList();
                if (rest.Count == 0) continue;
                Group(L.T(group));
                foreach (var item in rest)
                    Switch(item.Env);
            }
            ShowChanged();
        }

        public override void OnShown() => ShowChanged();

        private void Feature(SymbolRegular icon, string title, string hint, string key, Panel into, string? line = null) =>
            Tracked(icon, title, hint, key, into, line: line);

        /// <summary>A launcher setting's switch on this page: counted by Changed switches, put back by Reset all.</summary>
        private Wpf.Ui.Controls.ToggleSwitch Tracked(SymbolRegular icon, string title, string hint, string key, Panel? into = null, Action? changed = null,
            string? line = null)
        {
            var toggle = SettingToggle(icon, title, hint, key, into, () =>
            {
                changed?.Invoke();
                ShowChanged();
            }, line);
            _tracked.Add((key, title));
            _resets.Add(() =>
            {
                var on = DefaultFlag(key);
                S.Set(key, on);
                toggle.IsChecked = on;
            });
            return toggle;
        }

        /// <summary>A launcher setting's choice on this page, as Tracked.</summary>
        private void TrackedCombo(SymbolRegular icon, string title, string hint, IReadOnlyList<Choice> choices, string key, string? line = null)
        {
            var control = SettingCombo(icon, title, hint, choices, key, changed: ShowChanged, line: line);
            _tracked.Add((key, title));
            _resets.Add(() =>
            {
                S.Set(key, DefaultText(key));
                Pick(control, Choices.IndexOf(choices, DefaultText(key)));
            });
        }

        /// <summary>An engine switch's row (LauncherPage.SwitchRow), with its group's icon.</summary>
        private void Switch(string env)
        {
            var item = EngineSwitches.All.Single(x => x.Env == env);
            _switches.Add(item);
            _resets.Add(SwitchRow(item, GroupIcon(item.Group), changed: ShowChanged));
        }

        private static bool DefaultFlag(string key) =>
            LauncherSettings.DefaultOf(key) is System.Text.Json.Nodes.JsonValue v && v.TryGetValue<bool>(out var on) && on;

        private static string DefaultText(string key) =>
            LauncherSettings.DefaultOf(key) is System.Text.Json.Nodes.JsonValue v && v.TryGetValue<string>(out var text) ? text : "";

        private bool AtDefault(string key) =>
            LauncherSettings.DefaultOf(key) is System.Text.Json.Nodes.JsonValue v && v.TryGetValue<bool>(out var on)
                ? S.Bool(key) == on
                : S.Str(key) == DefaultText(key);

        /// <summary>The preset was picked: its switches show their new values.</summary>
        private static SymbolRegular GroupIcon(string group) => group switch
        {
            EngineSwitches.Graphics => SymbolRegular.Sparkle24,
            EngineSwitches.Performance => SymbolRegular.TopSpeed24,
            EngineSwitches.Input => SymbolRegular.XboxController24,
            EngineSwitches.Sound => SymbolRegular.Speaker224,
            EngineSwitches.Fixes => SymbolRegular.ShieldCheckmark24,
            _ => SymbolRegular.Bug24,
        };

        /// <summary>The summary row: what on this page is not at its default, and what Extra variables override.</summary>
        private void ShowChanged()
        {
            if (_changedHint == null) return; // rows built before the summary row exists
            var changed = Changed().ToList();
            var extra = GameEnvironment.ExtraVariables(S).Select(e => e.Name).ToHashSet(StringComparer.Ordinal);
            var overridden = EngineSwitches.Changed(S).Where(c => _switches.Any(x => x.Env == c.Name) && extra.Contains(c.Name))
                .Select(c => c.Name).ToList();
            var text = changed.Count == 0
                ? L.T("None: the engine runs with its own defaults")
                : L.F($"{changed.Count} changed: ") + string.Join(", ", changed);
            if (overridden.Count > 0)
                text += L.F($"\nExtra variables also set {string.Join(", ", overridden)}: their value is used");
            SetHint(_changedHint, text);
        }

        /// <summary>This page's rows not at their default: launcher settings by title, switches with the variable passed.</summary>
        private IEnumerable<string> Changed()
        {
            foreach (var (key, title) in _tracked)
                if (!AtDefault(key)) yield return title;
            foreach (var (name, value) in EngineSwitches.Changed(S))
                if (_switches.FirstOrDefault(x => x.Env == name) is { } item)
                    yield return $"{item.Title} ({name}={value})";
        }

        private void ResetAll()
        {
            if (!Changed().Any()) return;
            if (!Prompt.Confirm(L.T("Reset engine switches"), L.T("Every switch on this page goes back to the engine's default."), L.T("Reset all"))) return;
            // The switches' saved values first: a toggle set from code saves nothing itself.
            foreach (var item in _switches)
                S.Remove(item.Key);
            foreach (var reset in _resets)
                reset();
            Ws.SaveSoon();
            ShowChanged();
        }
    }
}
