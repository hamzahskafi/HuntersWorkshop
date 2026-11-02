using BloodborneLauncher.Core;
using Wpf.Ui.Controls;
using System.Windows;
using System.Windows.Controls;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// What the screen gets: the monitor, the window, the output resolution, the frame rate and V-Sync, HDR
    /// and picture tuning (0.4.4 beta 26: the FPS counter, Sharp UI and the in-game menu's language and
    /// shortcut went to Interface and the game's language to Play; 26b: HDR back here, Best settings to
    /// Graphics; 27b: picture tuning from the Look page, which went, below HDR).
    /// </summary>
    public sealed class DisplayPage : LauncherPage
    {
        private readonly ComboBox _monitor = new() { Width = 360, VerticalAlignment = VerticalAlignment.Center };
        private bool _refreshingMonitors;

        public override void OnShown()
        {
            RefreshMonitors();
            ShowHdr(); // Use HDR may have been turned on or off in Windows meanwhile
        }

        private void RefreshMonitors()
        {
            _refreshingMonitors = true;
            try
            {
                var saved = S.Str("display_device");
                _monitor.Items.Clear();
                _monitor.Items.Add(new ComboBoxItem { Content = L.T("Default (Windows primary monitor)"), Tag = "" });
                foreach (var monitor in WindowsMonitors.All())
                    _monitor.Items.Add(new ComboBoxItem { Content = monitor.Label, Tag = monitor.Device,
                        ToolTip = $"{monitor.Width} × {monitor.Height}; {monitor.Device}" });
                var selected = _monitor.Items.Cast<ComboBoxItem>().FirstOrDefault(i => string.Equals(i.Tag as string, saved, StringComparison.OrdinalIgnoreCase));
                if (selected == null && saved.Length > 0)
                {
                    selected = new ComboBoxItem { Content = L.F($"Disconnected monitor ({saved})"), Tag = saved };
                    _monitor.Items.Add(selected);
                }
                _monitor.SelectedItem = selected ?? _monitor.Items[0];
            }
            finally { _refreshingMonitors = false; }
        }

        /// <summary>Output resolution: a named size, the display's own, or Custom with its width and
        /// height (the engine takes any even WxH from 640 × 360 to 16384 × 8640).</summary>
        private void OutputResolutionRows()
        {
            static bool Named(string v) => v == "auto" || Choices.OutputResolutions.Any(c => c.Value == v && c.Value != Choices.Custom);
            var widthBox = new Wpf.Ui.Controls.TextBox { Width = 90, PlaceholderText = L.T("Width"), PlaceholderEnabled = true };
            var heightBox = new Wpf.Ui.Controls.TextBox { Width = 90, PlaceholderText = L.T("Height"), PlaceholderEnabled = true };
            var (w0, h0) = Choices.OutputSize(Ini.Get("output_res", "1920x1080"));
            widthBox.Text = w0.ToString();
            heightBox.Text = h0.ToString();
            SettingRow customRow = null!;
            System.Windows.Controls.TextBlock customNote = null!;

            void ApplyCustom()
            {
                if (!int.TryParse(widthBox.Text.Trim(), out var w) || !int.TryParse(heightBox.Text.Trim(), out var h) ||
                    !Choices.ValidCustomSize(w, h))
                {
                    SetHint(customNote, L.T("Width 640 to 16384, height 360 to 8640"));
                    return;
                }
                w &= ~1;
                h &= ~1;
                widthBox.Text = w.ToString();
                heightBox.Text = h.ToString();
                Ini.Set("output_res", $"{w}x{h}");
                SetHint(customNote, null);
                Ws.SaveSoon();
            }

            ComboRow(SymbolRegular.ResizeLarge24, L.T("Output resolution"), L.T("The upscaler renders below this and fills it in"),
                Choices.OutputResolutions,
                () => { var v = Ini.Get("output_res", "1920x1080"); return Named(v) ? v : Choices.Custom; },
                v =>
                {
                    customRow.Visibility = v == Choices.Custom ? Visibility.Visible : Visibility.Collapsed;
                    if (v == Choices.Custom) ApplyCustom();
                    else Ini.Set("output_res", v);
                });

            foreach (var box in new[] { widthBox, heightBox })
            {
                box.LostFocus += (_, _) => ApplyCustom();
                box.KeyDown += (_, e) => { if (e.Key == System.Windows.Input.Key.Enter) ApplyCustom(); };
            }
            customRow = Add(Card(SymbolRegular.Ruler24, L.T("Custom resolution"),
                L.T("Your own output size, any shape (5:4, 16:10, 32:9...): the camera and UI follow it with Widescreen on"),
                Row(widthBox, new System.Windows.Controls.TextBlock { Text = "×", Margin = new Thickness(8, 0, 8, 0), VerticalAlignment = VerticalAlignment.Center }, heightBox),
                out customNote));
            customRow.Visibility = Named(Ini.Get("output_res", "1920x1080")) ? Visibility.Collapsed : Visibility.Visible;
        }

        /// <summary>The frame rate and V-Sync, side by side (0.4.4 beta 22: V-Sync was the presentation
        /// mode under Screen › Advanced). Choosing Uncapped turns V-Sync Off and going back to a fixed
        /// rate turns that Off back on (FrameRateSync); V-Sync's note says when the pair holds the game
        /// to the display.</summary>
        private void FrameRateRows()
        {
            Group(L.T("Frame rate"));
            var refresh = Hardware.Detect().RefreshHz;
            var rate = S.Str("fps_mode");
            FrameworkElement sync = null!;
            SettingCombo(SymbolRegular.Gauge24, L.T("Frame rate"), L.T("Every rate above 30 runs on Kyo's 60 FPS++, the engine setting the game's values for the rate (Engine switches › Frame rate corrections). 30 is the game as on the PS4"),
                Choices.FpsModes, "fps_mode", changed: () =>
                {
                    var now = S.Str("fps_mode");
                    var after = FrameRateSync.After(rate, now, S.Str("present_mode"));
                    rate = now;
                    if (after != null) Pick(sync, Choices.IndexOf(Choices.PresentModes, after));
                    RefreshNotes();
                }, noteOf: c => c.Note);
            sync = SettingCombo(SymbolRegular.Image24, L.T("V-Sync"), L.T("How finished frames reach the screen"),
                Choices.PresentModes, "present_mode", noteOf: c => FrameRateSync.Note(S.Str("fps_mode"), c.Value, refresh) ?? c.Note);
        }

        public DisplayPage()
        {
            _monitor.SetResourceReference(StyleProperty, "DefaultComboBoxStyle");
            ComboSearch.Attach(_monitor);
            Group(L.T("Screen"));
            Add(Card(SymbolRegular.Desktop24, L.T("Monitor"),
                L.T("The game opens here from the start, including fullscreen and HDR. If this monitor is disconnected, the Windows primary monitor is used. Applied on the next launch."),
                _monitor, out _));
            RefreshMonitors();
            _monitor.SelectionChanged += (_, _) =>
            {
                if (_refreshingMonitors || _monitor.SelectedItem is not ComboBoxItem item) return;
                S.Set("display_device", item.Tag as string ?? "");
                Ws.SaveSoon();
                ShowHdr(); // HDR is the chosen display's
            };
            // One choice for what were two switches (Fullscreen, and Borderless fullscreen, which did
            // nothing without it). The same two settings as before: config.ini's fullscreen and the
            // launcher's borderless_fullscreen (BB_EXCLUSIVE_FULLSCREEN=0).
            ComboRow(SymbolRegular.FullScreenMaximize24, L.T("Window mode"), L.T("How the game's window fills the screen"),
                Choices.WindowModes,
                () => !Ini.Flag("fullscreen") ? "window" : S.Bool("borderless_fullscreen") ? "borderless" : "fullscreen",
                v =>
                {
                    Ini.Set("fullscreen", v == "window" ? "0" : "1");
                    if (v != "window") S.Set("borderless_fullscreen", v == "borderless");
                });
            OutputResolutionRows();
            ToggleRow(SymbolRegular.RatioOneToOne24, L.T("Widescreen"), L.T("Outputs that are not 16:9 (21:9, 32:9, 16:10, 5:4) with the right field of view and the UI kept in shape"),
                () => Ini.Get("widescreen", "1") != "0", v => Ini.Set("widescreen", v ? "1" : "0"));
            var screenMore = Advanced();
            IniCombo(SymbolRegular.ArrowSync24, L.T("Live resolution change"),
                L.T("Change the resolution in the game menu without restarting. Off is faster; turn on only if you switch resolutions in game"), Choices.LiveResolution, "live_resolution", into: screenMore);

            FrameRateRows();
            HdrRows();
            PictureRows();
        }

        private FoldRows _hdr = null!;
        private readonly StackPanel _hdrNative = new();
        private SettingRow _hdrRow = null!, _peakRow = null!;
        private Wpf.Ui.Controls.ToggleSwitch _hdrSwitch = null!;
        private static string HdrHint => L.T("The game's picture on an HDR screen, with brighter highlights. Applied when the game starts");

        /// <summary>
        /// HDR (0.4.4 beta 26b: back from the Picture page, beside the display it depends on, as in the
        /// in-game menu's Display tab). A switch with its settings folded under it; the display the game
        /// opens on (Monitor above) says whether it can: no HDR there greys the switch.
        /// </summary>
        private void HdrRows()
        {
            Group("HDR");
            _hdr = FoldToggle(SymbolRegular.Hdr24, L.T("HDR output"), HdrHint,
                () => Ini.Flag("hdr"), v => Ini.Set("hdr", v ? "1" : "0"), out _hdrSwitch, out _hdrRow, changed: ShowHdr);
            ComboRow(SymbolRegular.Lightbulb24, L.T("HDR source"),
                L.T("Native: highlights as bright as they are in the game's scene. Expanded: the SDR picture's bright parts made brighter"),
                Choices.HdrMode, () => Ini.Get("hdr_mode", "native"), v => Ini.Set("hdr_mode", v), _hdr, changed: ShowHdr);
            // The UI's brightness and colour apply to the Native source only (the in-game menu hides them
            // otherwise): under it (0.4.4 beta 27b).
            ComboRow(SymbolRegular.TextFont24, L.T("UI brightness"), L.T("Native source only: how bright the HUD, menus and subtitles are, against paper white"),
                Choices.HdrUiBrightness, () => Ini.Get("hdr_ui_brightness", "1"), v => Ini.Set("hdr_ui_brightness", v), _hdrNative);
            Nest();
            ComboRow(SymbolRegular.Color24, L.T("UI colour"), L.T("Native source only: Vivid shows the HUD and menus with the colours a wide-gamut monitor gives SDR; Exact keeps sRGB's"),
                Choices.HdrUiColor, () => Ini.Get("hdr_ui_color", "1"), v => Ini.Set("hdr_ui_color", v), _hdrNative);
            Nest();
            _hdr.Children.Add(_hdrNative);
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
            _peakRow = Nest();
            ComboRow(SymbolRegular.Sparkle24, L.T("Highlight boost"), L.T("Off keeps the game's own look; higher makes lamps, fire and the sky brighter"),
                Choices.HdrBoost, () => Ini.Get("hdr_boost", "0.25"), v => Ini.Set("hdr_boost", v), _hdr);
            Body.Children.Add(_hdr);
            ShowHdr();
        }

        private readonly List<Action> _gameOwnPicture = new();
        private FoldRows _picture = null!;

        /// <summary>
        /// Picture tuning (0.4.4 beta 27b: from the Look page, which went, below HDR as the tester asked):
        /// the engine's picture controls (bbport_settings.cpp clamps the same ranges), in the in-game menu's
        /// order, with its "Game's own picture" button, folded under a switch. Its values apply only while
        /// it is on; off is the game's own picture with the values kept, as in the game's System menu
        /// (Colour), which has the same switch.
        /// </summary>
        private void PictureRows()
        {
            Group(L.T("Picture"));
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

        /// <summary>What the game's display can do, and the rows that depend on choices made.</summary>
        private void ShowHdr()
        {
            if (_hdrRow == null) return; // rows built after the monitor list
            // The display the game opens on; when it cannot be told, any display decides, as before.
            var display = WindowsHdr.ForMonitor(S.Str("display_device"));
            var supported = display?.Supported ?? WindowsHdr.AnySupported();
            var on = display?.On ?? WindowsHdr.AnyOn();
            // Greyed only when that display is known to show no HDR; a saved On can still be turned off.
            _hdrSwitch.IsEnabled = display == null || supported || Ini.Flag("hdr");
            SetHint(_hdrRow.Hint, !supported ? L.T("This display shows no HDR")
                : !on ? L.T("Turn on Use HDR in Windows' display settings first (Settings › System › Display)")
                : HdrHint);
            _peakRow.IsEnabled = Ini.Get("hdr_peak_auto", "1") == "0";
            _hdrNative.Visibility = Ini.Get("hdr_mode", "native") == "native" ? Visibility.Visible : Visibility.Collapsed;
        }
    }
}
