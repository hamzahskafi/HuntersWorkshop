using BloodborneLauncher.Core;
using Wpf.Ui.Controls;
using System.Windows;
using System.Windows.Controls;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// Where and how the game is shown: the monitor, the window, the output resolution, the frame rate
    /// and V-Sync (0.4.4 beta 26: the FPS counter, Sharp UI and the in-game menu's language and shortcut
    /// went to Interface, HDR to Picture and the game's language to Play).
    /// </summary>
    public sealed class DisplayPage : LauncherPage
    {
        private readonly ComboBox _monitor = new() { Width = 360, VerticalAlignment = VerticalAlignment.Center };
        private bool _refreshingMonitors;

        public override void OnShown() => RefreshMonitors();

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

        public DisplayPage() : base(L.T("Display"), "")
        {
            _monitor.SetResourceReference(StyleProperty, "DefaultComboBoxStyle");
            ComboSearch.Attach(_monitor);
            // Sets this page's resolution and frame rate and the Upscaling page's upscaler (0.4.4 beta 26:
            // was at the top of the Graphics page).
            Add(Card(SymbolRegular.Gauge24, L.T("Best settings for this PC"),
                L.T("The output resolution, frame rate, upscaler and quality preset that suit this PC; you see each change before it is applied") +
                "\n\n" + Hardware.Detect().Summary,
                SmallButton(L.T("Recommend…"), SymbolRegular.Sparkle24, (_, _) => MainWindow.Instance!.OfferBestSettings(firstRun: false)), out _));

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
        }
    }
}
