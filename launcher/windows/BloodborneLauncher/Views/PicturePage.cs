using System.Windows;
using System.Windows.Controls;
using BloodborneLauncher.Core;
using Wpf.Ui.Controls;
using TextBlock = System.Windows.Controls.TextBlock;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// How the game's picture looks: HDR output, the engine's picture tuning and the game's own effects
    /// (0.4.4 beta 26: HDR was on the Display page, picture tuning and the effects on Graphics, texture
    /// filtering and the motion blur switch among the engine switches). Each group's settings are greyed
    /// while its own switch is off.
    /// </summary>
    public sealed class PicturePage : LauncherPage
    {
        private readonly StackPanel _hdr = new(), _hdrNative = new(), _picture = new();
        private readonly SettingRow _peakRow;
        private readonly TextBlock _motionBlurHint;
        private readonly List<Action> _gameOwnPicture = new();

        public PicturePage() : base(L.T("Picture"), "")
        {
            // ==================== HDR ====================
            Group("HDR");
            var hdrHint = WindowsHdr.AnyOn()
                ? L.T("The game's picture on an HDR screen, with brighter highlights. Applied when the game starts")
                : WindowsHdr.AnySupported()
                    ? L.T("Turn on Use HDR in Windows' display settings first (Settings › System › Display)")
                    : L.T("This display shows no HDR");
            IniToggle(SymbolRegular.Hdr24, L.T("HDR output"), hdrHint, "hdr", changed: ShowHdr);
            ComboRow(SymbolRegular.Lightbulb24, L.T("HDR source"),
                L.T("Native: highlights as bright as they are in the game's scene. Expanded: the SDR picture's bright parts made brighter"),
                Choices.HdrMode, () => Ini.Get("hdr_mode", "native"), v => Ini.Set("hdr_mode", v), _hdr, changed: ShowHdr);
            SliderRow(SymbolRegular.BrightnessHigh24, L.T("Paper white"), L.T("How bright the game's white is, in nits. Range: 10–500"),
                10, 500, 1, () => ParseDouble(Ini.Get("hdr_paper_white", "200"), 200),
                v => Ini.Set("hdr_paper_white", v.ToString("0", System.Globalization.CultureInfo.InvariantCulture)),
                format: L.T("0' nits'"), into: _hdr, valueWidth: 82);
            ToggleRow(SymbolRegular.Desktop24, L.T("Peak brightness from the display"),
                L.T("The peak Windows reports for the display the game is on (the Windows HDR Calibration app's, once it has run). Off: the value below"),
                () => Ini.Get("hdr_peak_auto", "1") != "0", v => Ini.Set("hdr_peak_auto", v ? "1" : "0"), _hdr, ShowHdr);
            SliderRow(SymbolRegular.WeatherSunny24, L.T("Peak brightness"), L.T("Your display's peak brightness, in nits, when it is not taken from the display. Range: 10–4000"),
                10, 4000, 10, () => ParseDouble(Ini.Get("hdr_peak", "1000"), 1000),
                v => Ini.Set("hdr_peak", v.ToString("0", System.Globalization.CultureInfo.InvariantCulture)),
                format: L.T("0' nits'"), into: _hdr, valueWidth: 82);
            _peakRow = (SettingRow)_hdr.Children[^1];
            ComboRow(SymbolRegular.Sparkle24, L.T("Highlight boost"), L.T("Off keeps the game's own look; higher makes lamps, fire and the sky brighter"),
                Choices.HdrBoost, () => Ini.Get("hdr_boost", "0.25"), v => Ini.Set("hdr_boost", v), _hdr);
            // The UI's brightness and colour apply to the Native source only (the in-game menu hides them otherwise).
            ComboRow(SymbolRegular.TextFont24, L.T("UI brightness"), L.T("Native source only: how bright the HUD, menus and subtitles are, against paper white"),
                Choices.HdrUiBrightness, () => Ini.Get("hdr_ui_brightness", "1"), v => Ini.Set("hdr_ui_brightness", v), _hdrNative);
            ComboRow(SymbolRegular.Color24, L.T("UI colour"), L.T("Native source only: Vivid shows the HUD and menus with the colours a wide-gamut monitor gives SDR; Exact keeps sRGB's"),
                Choices.HdrUiColor, () => Ini.Get("hdr_ui_color", "1"), v => Ini.Set("hdr_ui_color", v), _hdrNative);
            _hdr.Children.Add(_hdrNative);
            Body.Children.Add(_hdr);
            ShowHdr();

            // ==================== Picture tuning ====================
            // The engine's picture controls (bbport_settings.cpp clamps the same ranges), in the in-game
            // menu's order, with its "Game's own picture" button.
            Group(L.T("Picture tuning"));
            IniToggle(SymbolRegular.Color24, L.T("Picture tuning"),
                L.T("Turns the settings below on, live. Also in the game's System menu (PC Graphics, PC Effects, PC Colour) and the Insert menu"),
                "graphics_controls", changed: ShowPicture);
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
            ShowPicture();

            // ==================== Game effects ====================
            Group(L.T("Game effects"));
            Body.Children.Add(Text(L.T("Patches to the game, applied when it starts."), "Faint"));
            IniCombo(SymbolRegular.Cube24, L.T("Model detail"), null, Choices.ModelLod, "model_lod");
            SwitchRow(EngineSwitches.All.Single(s => s.Env == "BB_ANISO"), SymbolRegular.Image24);
            TextBlock? blurHint = null;
            foreach (var effect in Choices.Effects.Where(e => !Gameplay.Contains(e.Key)))
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
        }

        /// <summary>The effects the Gameplay page shows (patches too, but not to the picture).</summary>
        public static readonly string[] Gameplay = { "skip_intro", "debug_camera", "debug_menu" };

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

        private void ShowHdr()
        {
            _hdr.IsEnabled = Ini.Flag("hdr");
            _peakRow.IsEnabled = Ini.Get("hdr_peak_auto", "1") == "0";
            _hdrNative.Visibility = Ini.Get("hdr_mode", "native") == "native" ? Visibility.Visible : Visibility.Collapsed;
        }

        private void ShowPicture() => _picture.IsEnabled = Ini.Flag("graphics_controls");

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
