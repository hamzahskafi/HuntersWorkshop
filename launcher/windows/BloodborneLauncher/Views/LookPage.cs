using System.Windows;
using System.Windows.Controls;
using BloodborneLauncher.Core;
using Wpf.Ui.Controls;
using TextBlock = System.Windows.Controls.TextBlock;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// The game's style, a matter of taste that costs next to nothing (0.4.4 beta 26b, was the Picture
    /// page; HDR went to Display, the detail that costs frames to Graphics): the lens effects, and picture
    /// tuning, a switch whose settings fold under it. Its values apply only while it is on; off is the
    /// game's own picture with the values kept, as in the game's System menu (Colour), which has the same
    /// switch.
    /// </summary>
    public sealed class LookPage : LauncherPage
    {
        /// <summary>The game's effects shown here: the lens and camera effects people turn off or on by taste.</summary>
        public static readonly string[] LookEffects = { "effect_dof", "effect_motion_blur", "effect_chromatic_aberration" };

        private readonly TextBlock _motionBlurHint;
        private readonly List<Action> _gameOwnPicture = new();
        private FoldRows _picture = null!;

        public LookPage() : base(L.T("Look"), "")
        {
            // ==================== Effects ====================
            Group(L.T("Effects"));
            Body.Children.Add(Text(L.T("Patches to the game, applied when it starts."), "Faint"));
            TextBlock? blurHint = null;
            foreach (var effect in Choices.Effects.Where(e => LookEffects.Contains(e.Key)))
            {
                var motionBlur = effect.Key == "effect_motion_blur";
                IniToggle(SymbolRegular.Color24, effect.Title, effect.Hint, effect.Key, changed: motionBlur ? ShowMotionBlur : null);
                if (!motionBlur) continue;
                blurHint = ((SettingRow)Body.Children[^1]).Hint;
                // Beside the blur it is about (was among the engine switches).
                SwitchRow(EngineSwitches.All.Single(s => s.Env == "BB_UPSCALER_NO_BLUR"), SymbolRegular.Color24);
            }
            _motionBlurHint = blurHint!;
            ShowMotionBlur();

            // ==================== Picture tuning ====================
            // The engine's picture controls (bbport_settings.cpp clamps the same ranges), in the in-game
            // menu's order, with its "Game's own picture" button.
            Group(L.T("Picture tuning"));
            _picture = FoldToggle(SymbolRegular.Color24, L.T("Picture tuning"),
                L.T("Applies the settings below, live; off shows the game's own picture and keeps your values. Also in the game's System menu (Graphics, Effects, Colour) and the Insert menu"),
                () => Ini.Flag("graphics_controls"), v => Ini.Set("graphics_controls", v ? "1" : "0"), out _, out _);
            TuningSlider(SymbolRegular.BrightnessHigh24, L.T("Brightness"), 0, 3, "graphics_brightness");
            TuningSlider(SymbolRegular.DarkTheme24, L.T("Contrast"), 0.8, 1.2, "graphics_contrast", step: 0.01);
            TuningSlider(SymbolRegular.Color24, L.T("Saturation"), 0, 3, "graphics_saturation");
            TuningSlider(SymbolRegular.Color24, L.T("Hue"), -180, 180, "graphics_hue", step: 1, fallback: 0);
            TuningSlider(SymbolRegular.WeatherFog24, L.T("Fog"), 0, 3, "graphics_fog");
            TuningSlider(SymbolRegular.Lightbulb24, L.T("Bloom"), 0, 3, "graphics_bloom");
            // On unless turned off: the engine's default (bbport_settings.h).
            var vignette = ToggleRow(SymbolRegular.CircleHalfFill24, L.T("Vignette"), null,
                () => Ini.Get("graphics_vignette", "1") != "0", v => Ini.Set("graphics_vignette", v ? "1" : "0"), _picture);
            _gameOwnPicture.Add(() =>
            {
                vignette.IsChecked = true;
                Ini.Set("graphics_vignette", "1");
            });
            TuningSlider(SymbolRegular.DarkTheme24, L.T("Ambient occlusion strength"), 0, 3, "graphics_ao_strength");
            TuningSlider(SymbolRegular.WeatherSunny24, L.T("Shadow distance"), 0.5, 4, "graphics_shadow_scale");
            Add(Card(SymbolRegular.ArrowReset24, L.T("Game's own picture"), L.T("Puts the settings above back to the game's own values"),
                SmallButton(L.T("Reset"), SymbolRegular.ArrowUndo24, (_, _) =>
                {
                    foreach (var reset in _gameOwnPicture) reset();
                    Ws.SaveSoon();
                }), out _), _picture);
            Body.Children.Add(_picture);
        }

        public override void OnShown() => ShowMotionBlur();

        /// <summary>A picture tuning slider; the "Game's own picture" button puts it back to <paramref name="fallback"/>.</summary>
        private void TuningSlider(SymbolRegular icon, string title, double min, double max, string key, double step = 0.05, double fallback = 1.0)
        {
            var slider = SliderRow(icon, title, null, min, max, step, () => ParseDouble(Ini.Get(key, Fmt(fallback)), fallback),
                v => Ini.Set(key, Fmt(v)), into: _picture);
            _gameOwnPicture.Add(() =>
            {
                slider.Value = fallback; // saves through the slider
                Ini.Set(key, Fmt(fallback));
            });
        }

        /// <summary>An upscaler takes the finished frame at outputs other than 1920x1080 and can smear the
        /// game's motion blur; the switch under it leaves the blur out there (BB_UPSCALER_NO_BLUR).</summary>
        private void ShowMotionBlur()
        {
            if (_motionBlurHint == null) return; // the row is built after the first call
            var (w, h) = Choices.OutputSize(Ini.Get("output_res", "1920x1080"));
            SetHint(_motionBlurHint, Ini.Get("upscaler") != "off" && (w, h) != (1920, 1080) && Ini.Flag("effect_motion_blur")
                ? L.T("With an upscaler at an output other than 1920x1080 the blur can smear: No motion blur smear with upscalers, below, leaves it out there")
                : null);
        }
    }
}
