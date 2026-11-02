using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace BloodborneLauncher.Core
{
    /// <summary>
    /// settings.json, shared with the Windows port's own launcher (same folder, same keys).
    /// Unknown keys are kept, so neither launcher loses the other's settings.
    /// </summary>
    public sealed class LauncherSettings
    {
        public const string OnlineCommunity = "https://thehuntersdream.com";

        private JsonObject _data = new();

        private static Dictionary<string, JsonNode?> Defaults() => new()
        {
            ["ui_language"] = "en",
            ["game_dir"] = "",
            ["user_dir"] = "",
            ["dlc_dir"] = "",
            ["player_profile"] = "",
            ["player_profiles"] = "",
            ["mods_dir"] = "",
            ["mods_enabled"] = true,
            ["patches_dir"] = "",
            ["language"] = "1",
            ["display_device"] = "",
            ["present_mode"] = "Mailbox",
            ["fps_mode"] = "60",
            ["draw_pipe"] = "",
            ["readbacks"] = "",
            // Beta builds: on (Load switches older files once), so crash logs carry the frame stats.
            ["frame_stats"] = Updater.CurrentBeta > 0,
            // Settings › Log, capture and extra variables (0.4.0): the log level, hitch reports
            // (0: off; else ms) and the controller details.
            ["log_level"] = "standard",
            ["hitch_report_ms"] = "0",
            ["pad_log"] = false,
            ["gpu_profile"] = false,
            ["extra_env"] = "",
            ["dlc_enabled"] = true,
            // This launcher.
            ["port_dir"] = "",
            ["launch_method"] = "windows",
            ["custom_command"] = "",
            ["custom_args"] = "",
            ["switch_to_output"] = true,
            ["check_updates"] = true,
            ["save_backups"] = true,
            ["safe_mode_next"] = false,
            ["memory_check_off"] = false,
            ["hardware_offered"] = false,
            ["setup_shown"] = false, // the setup guide opened once (Views/SetupGuide.cs)
            ["cheat_vials_amount"] = 20,
            ["cheat_bullets_amount"] = 20,
            ["update_channel"] = "", // empty: beta builds follow beta, others stable
            ["ui_layout"] = "classic", // classic | modern
            ["ui_navigation"] = "tabs", // tabs | sidebar (Modern layout; Classic has the sidebar)
            ["ui_theme"] = "default",
            ["ui_theme_colour"] = "#C41E3A",
            ["ui_background"] = "game", // game | custom | none
            ["ui_background_image"] = "",
            ["ui_background_blur"] = "strong", // strong | light | off
            ["app_icon"] = "auto", // auto | red | blue | j3loo | custom (AppIcon)
            ["app_icon_image"] = "", // custom: the picture chosen (AppIcon.CustomFile holds it as an icon)
            ["ui_window_buttons"] = "windows", // windows | mac
            ["ui_window_buttons_side"] = "usual", // usual (mac left, windows right) | left | right
            ["exp_vram_cleanup"] = true,
            // 0.9.37 beta 4: the dev setup, measured faster (PerfDefaults below switches older files once).
            ["exp_frame_pacing"] = true,
            ["exp_vblank_lock"] = false, // frames locked to the display's refresh (BB_VBLANK_PACING=1)
            ["exp_particle_cap"] = false, // indirect dispatch counts clamped (BB_INDIRECT_CLAMP=1)
            ["exp_thread_priority"] = true,
            ["exp_sleep_spin"] = true,
            ["exp_async_pipelines"] = true,
            ["exp_fast_loading"] = true, // does nothing at 30 or 60 FPS (loading.cpp)
            ["exp_frame_fixes"] = true, // the frame-rate corrections at 60 FPS and above (frame_rate.cpp)
            ["exp_hot_pages"] = true,
            // 0.4.4: the game's per-frame GPU memory (its GX region) read in place (BB_GX_IN_PLACE).
            ["exp_gx_in_place"] = true,
            ["exp_native_calls"] = true,
            ["exp_sfx_workaround"] = false,
            ["borderless_fullscreen"] = false,
            // The new engine features (EnginePreset New): on since 0.9.34, stable builds too.
            // Beta 9.9: off by default (objects and floors popped in late or flickered).
            ["exp_occlusion"] = false,
            ["exp_fast_draws"] = true,
            ["exp_compile_skip"] = true,
            ["exp_vram_budget"] = true,
            // Still being tested: on in beta builds only.
            ["exp_sharp_ui"] = Updater.CurrentBeta > 0,
            ["audio_priority"] = false,
            ["sound_output"] = "",
            ["load_profile"] = false,
            ["section_stats"] = false,
            ["shader_capture"] = false,
            ["heap_checks"] = false, // 0.4.4 beta 28: BB_DIAG=heap
            ["overlay_keys"] = "both",
            ["dlss_preset"] = "k",
            ["legacy_shaders"] = false, // beta 9.8 made legacy the default; beta 4 the native one again (PerfDefaults)
            ["trophy_notifications"] = true,
            ["trophy_sound"] = true,
            ["trophy_volume"] = 70,
            ["trophy_position"] = "top-right",
        };

        public void Load()
        {
            _data = new JsonObject();
            foreach (var (key, value) in Defaults())
                _data[key] = value;

            try
            {
                if (File.Exists(Paths.ConfigFile) &&
                    JsonNode.Parse(File.ReadAllText(Paths.ConfigFile)) is JsonObject saved)
                {
                    foreach (var (key, value) in saved)
                        _data[key] = value?.DeepClone();
                    // Beta builds: textures kept up to 80% of the card (the 40% default evicted and
                    // reloaded hundreds a second on 6-8 GB cards); switched on once for older files.
                    // The marker is only ever written by a beta, so a player moving from a stable build
                    // to a beta gets it too (RTX 4060 at 3.4-4.3 GB of 2486 MiB with the 40% marks:
                    // evictions, re-uploads and tiled write-backs every frame).
                    if (Updater.CurrentBeta > 0 && !saved.ContainsKey("vram_budget_beta_on"))
                        _data["exp_vram_budget"] = true;
                    // 0.9.37 beta 8: beta builds write the frame statistics into the log, switched
                    // on once. Ebrietas' device losses were told apart only in the one log that had
                    // them (the game's GPU range allocator nearly empty); most reports came without.
                    if (Updater.CurrentBeta > 0 && !saved.ContainsKey("frame_stats_beta_on"))
                        _data["frame_stats"] = true;
                    // 0.4.4 beta 28: the unified frame time is part of Frame rate corrections (it works only
                    // with them, and every log since 0.3.0 ran with both): its own switch and marker go.
                    _data.Remove("exp_frame_timer");
                    _data.Remove("frame_timer_on_044");
                    // 0.4.0: a stable launcher over a beta's file (any beta writes frame_stats_beta_on)
                    // switches off once what betas turn on for testing: the frame statistics and the
                    // sharp UI. On again only by choice.
                    if (Updater.CurrentBeta == 0 && saved.ContainsKey("frame_stats_beta_on") &&
                        !saved.ContainsKey(BetaTestingOffMarker))
                    {
                        _data["frame_stats"] = false;
                        _data["exp_sharp_ui"] = false;
                    }
                }
            }
            catch
            {
                // A broken file falls back to the defaults; it is rewritten on the next save.
            }
            // Saved with the next change: from then on the player's own choice stays.
            if (Updater.CurrentBeta > 0)
            {
                _data["vram_budget_beta_on"] = true;
                _data["frame_stats_beta_on"] = true;
            }
            if (Updater.CurrentBeta == 0) _data[BetaTestingOffMarker] = true;
        }

        private const string BetaTestingOffMarker = "beta_testing_off_040";

        public void Save()
        {
            Directory.CreateDirectory(Paths.ConfigDir);
            File.WriteAllText(Paths.ConfigFile,
                _data.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        }

        /// <summary>The Hunter's Dream account token (its device link), encrypted like the password.</summary>
        public string HuntersDreamToken
        {
            get => Secret.Unprotect(Str("hd_token"));
            set
            {
                Set("hd_token", value.Length == 0 ? "" : Secret.Protect(value) is { Length: > 0 } encrypted ? encrypted : value);
                // A new sign-in (or none): a refusal was for the token before it.
                Set("hd_refused", "");
            }
        }

        /// <summary>Why The Hunter's Dream refused this PC's sign-in in a game (hd_refused, the server's
        /// reason), until the player signs in again; empty when it did not.</summary>
        public string HuntersDreamRefused => Str("hd_refused");

        public string Str(string key)
        {
            var node = _data[key];
            if (node is JsonValue v)
            {
                if (v.TryGetValue<string>(out var s)) return s;
                return v.ToJsonString().Trim('"');
            }
            return "";
        }

        public bool Bool(string key)
        {
            var node = _data[key];
            if (node is JsonValue v)
            {
                if (v.TryGetValue<bool>(out var b)) return b;
                if (v.TryGetValue<int>(out var i)) return i != 0;
                if (v.TryGetValue<string>(out var s)) return s is "1" or "true" or "True";
            }
            return false;
        }

        public int Int(string key)
        {
            var node = _data[key];
            if (node is JsonValue v)
            {
                if (v.TryGetValue<int>(out var i)) return i;
                if (v.TryGetValue<double>(out var d)) return (int)d;
                if (v.TryGetValue<string>(out var s) && int.TryParse(s, out i)) return i;
            }
            return 0;
        }

        public void Set(string key, string value) => _data[key] = value;

        /// <summary>A setting's default, as Load seeds it (the Engine page's Reset all); null when it has none.</summary>
        public static JsonNode? DefaultOf(string key) => Defaults().TryGetValue(key, out var value) ? value?.DeepClone() : null;

        /// <summary>Forgets a setting: it reads as absent ("" / false / 0) again.</summary>
        public void Remove(string key) => _data.Remove(key);

        /// <summary>Every setting as stored (shared settings).</summary>
        public IEnumerable<KeyValuePair<string, JsonNode?>> All => _data;

        public void SetNode(string key, JsonNode value) => _data[key] = value.DeepClone();
        public void Set(string key, bool value) => _data[key] = value;
        public void Set(string key, int value) => _data[key] = value;
    }
}
