using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using BloodborneLauncher.Core;
using Wpf.Ui.Controls;
using TextBlock = System.Windows.Controls.TextBlock;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// What the game renders (0.4.4 beta 27b; 27c after the tester's review). Anti-aliasing first, then the
    /// upscaler with what belongs to it under it: its quality (Auto by the output's height) and the render
    /// resolution, a slider that shows what the quality renders at and that only Custom moves; the FSR
    /// version from what AMD offers on this GPU (AmdFsrVersions); DLSS's model. Sharpening, the fine
    /// settings under Advanced, then Detail and the game's effects. A row that only matters for another's
    /// choice sits under it (Nest). One short line per row, the details in its tooltip. Frame generation
    /// is not offered (community-fixes-plan.md, Decisions).
    /// </summary>
    public sealed class GraphicsPage : LauncherPage
    {
        /// <summary>The game's effects in Detail: what costs frames or sets detail.</summary>
        public static readonly string[] DetailEffects = { "effect_dynamic_shadows", "effect_ssao", "effect_ssr" };
        /// <summary>The game's effects in Effects: the lens and camera effects people turn off or on by taste.</summary>
        public static readonly string[] LookEffects = { "effect_dof", "effect_motion_blur", "effect_chromatic_aberration" };

        private readonly FrameworkElement _aa;
        private readonly SettingRow _aaRow, _qualityRow, _scaleRow, _fsrRow, _dlssModel, _dlssTextures, _dlssExposure;
        private readonly SettingRow _sharpenCard, _sharpnessCard, _mipBiasCard, _motionVectors;
        private readonly TextBlock _aaHint, _upscalerHint, _scaleHint, _fsrHint, _dlssHint, _motionBlurHint;
        private readonly ComboBox _quality = new() { Width = 240, VerticalAlignment = VerticalAlignment.Center };
        private readonly ComboBox _fsr = new() { Width = 240, VerticalAlignment = VerticalAlignment.Center };
        private readonly Slider _scale = new()
        {
            Minimum = 33, Maximum = 100, SmallChange = 1, LargeChange = 5, TickFrequency = 1, IsSnapToTickEnabled = true,
            Width = 200, VerticalAlignment = VerticalAlignment.Center,
        };
        private readonly TextBlock _scaleText = new() { MinWidth = 150, Margin = new Thickness(12, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
        private List<string> _fsrValues = new();
        private bool _syncing;

        public GraphicsPage() : base(L.T("Graphics"), "")
        {
            // Sets the Display page's resolution and frame rate and this page's upscaler and quality.
            Add(Card(SymbolRegular.Gauge24, L.T("Best settings for this PC"),
                L.T("Output resolution, frame rate, upscaler and quality for this PC") + "\n\n" + Hardware.Detect().Summary,
                SmallButton(L.T("Recommend…"), SymbolRegular.Sparkle24, (_, _) => MainWindow.Instance!.OfferBestSettings(firstRun: false)), out _));

            Group(L.T("Upscaling and anti-aliasing"));
            // 0.4.4 beta 27c: first (the tester's order). The anti-aliasing without an upscaler, the game's
            // own pass among them: greyed, nothing chosen, while an upscaler anti-aliases itself (the engine
            // leaves the game's pass out then: patches.cpp NoUpscaler).
            _aa = ComboRow(SymbolRegular.Color24, L.T("Anti-aliasing"), L.T("Smooths edges when no upscaler runs"),
                Choices.AntiAliasing, AntiAliasingNow, v =>
                {
                    if (_syncing) return; // shown again from config.ini, not picked
                    Ini.Set("upscaler", v == "taa" ? "taa" : "off");
                    Ini.Set("effect_game_aa", v == "game" ? "1" : "0");
                }, changed: () => { if (!_syncing) UpdateUpscaler(); });
            _aaRow = LastRow;
            _aaHint = (TextBlock)_aaRow.Tag;

            // The upscalers, or None, where Anti-aliasing above decides (upscaler=taa or off). One FSR,
            // AMD's, through AMD's FSR SDK (engine\amd-fsr).
            ComboRow(SymbolRegular.Sparkle24, L.T("Upscaler"), L.T("Renders below the output and fills it in"),
                Choices.FrameUpscalers, () => Ini.Get("upscaler") is "fsr" or "dlss" ? Ini.Get("upscaler") : "none",
                v => Ini.Set("upscaler", v != "none" ? v : Ini.Flag("effect_game_aa") ? "off" : "taa"), changed: UpdateUpscaler);
            _upscalerHint = (TextBlock)LastRow.Tag;

            // Its quality: plain names, Auto first with what it picks for the output (ShowQuality).
            foreach (var _ in Choices.Presets) _quality.Items.Add(new ComboBoxItem());
            ComboSearch.Attach(_quality);
            _qualityRow = Add(Card(SymbolRegular.ZoomIn24, L.T("Quality"),
                L.T("How far below the output the game renders: lower is faster and softer. Auto: Native AA up to 1200 lines, Quality up to 1600, Performance above"),
                _quality, out _));
            _qualityRow.Choices = string.Join(" · ", Choices.Presets.Select(c => c.Label));
            Nest();
            _quality.SelectionChanged += (_, _) =>
            {
                if (_syncing || _quality.SelectedIndex < 0) return;
                Ini.Set("preset", Choices.Presets[_quality.SelectedIndex].Value);
                Ws.SaveSoon();
                UpdateUpscaler();
            };

            // Under it, what the quality renders at: a slider (the share of the output per side) that only
            // Custom moves; render_scale keeps it as the engine's ratio (77% is 1.30).
            _scaleText.SetResourceReference(TextBlock.FontFamilyProperty, "MonoFont");
            _scaleRow = Add(Card(SymbolRegular.SlideSize24, L.T("Render resolution"),
                L.T("The share of the output's width and height the game renders at. DLSS runs the nearest quality's model"),
                Row(_scale, _scaleText), out _scaleHint));
            Nest();
            Nest();
            _scale.ValueChanged += (_, _) =>
            {
                if (_syncing || Ini.Get("preset", "auto") != "5") return;
                Ini.Set("render_scale", Fmt(100.0 / _scale.Value));
                Ws.SaveSoon();
                ShowQuality();
            };

            // AMD FSR's version: Automatic or any AMD offers on this GPU (asked in the background).
            ComboSearch.Attach(_fsr);
            _fsrRow = Add(Card(SymbolRegular.BrainCircuit24, L.T("FSR version"), L.T("The versions AMD offers on this GPU; Automatic runs the newest"),
                _fsr, out _fsrHint));
            Nest();
            _fsr.SelectionChanged += (_, _) =>
            {
                if (_syncing || _fsr.SelectedIndex < 0) return;
                Ini.Set("fsr_version", _fsrValues[_fsr.SelectedIndex]);
                Ws.SaveSoon();
                ShowFsrVersions();
            };

            // DLSS's model, with DLSS only (Auto, the default: NVIDIA's pairing for each quality).
            IniCombo(SymbolRegular.BrainCircuit24, L.T("DLSS model"), L.T("Which NVIDIA model DLSS runs; also in game (Insert or L3 + R3)"),
                Choices.DlssModels, "dlss_preset", changed: UpdateUpscaler);
            _dlssModel = Nest();
            _dlssHint = (TextBlock)_dlssModel.Tag;

            IniToggle(SymbolRegular.Eyedropper24, L.T("Sharpening (RCAS)"), L.T("Turn it up if the picture looks soft"), "sharpen",
                changed: UpdateUpscaler);
            _sharpenCard = LastRow;
            SliderRow(SymbolRegular.SlideSize24, L.T("Sharpening strength"), L.T("Above 1 adds an extra pass"), 0, 2, 0.05,
                () => ParseDouble(Ini.Get("sharpness"), 0.5), v => Ini.Set("sharpness", Fmt(v)));
            _sharpnessCard = Nest();

            // The fine settings most players leave alone; DLSS's own two with DLSS only.
            var fine = Advanced();
            IniToggle(SymbolRegular.ArrowMove24, L.T("Character motion vectors"), L.T("Less ghosting on characters; costs about 10% FPS"), "object_motion", fine);
            _motionVectors = LastRow;
            SliderRow(SymbolRegular.Image24, L.T("Texture mip bias"), L.T("Below 0 sharper textures, above 0 softer"),
                -2, 1, 0.05, () => ParseDouble(Ini.Get("mip_bias", "0"), 0), v => Ini.Set("mip_bias", Fmt(v)), format: "+0.00;-0.00;0.00", into: fine, valueWidth: 56);
            _mipBiasCard = LastRow;
            ToggleRow(SymbolRegular.Image24, L.T("DLSS sharper textures"), L.T("One more mip level of texture detail, as NVIDIA advises"),
                () => Ini.Get("dlss_mip_bias", "1") != "0", v => Ini.Set("dlss_mip_bias", v ? "1" : "0"), fine);
            _dlssTextures = LastRow;
            ToggleRow(SymbolRegular.BrightnessHigh24, L.T("DLSS auto exposure"), L.T("Can calm ghosting in dark scenes"),
                () => Ini.Get("dlss_auto_exposure", "0") == "1", v => Ini.Set("dlss_auto_exposure", v ? "1" : "0"), fine);
            _dlssExposure = LastRow;

            // ==================== Detail ====================
            Group(L.T("Detail"));
            Body.Children.Add(Text(L.T("Patches to the game, applied when it starts."), "Faint"));
            IniCombo(SymbolRegular.Cube24, L.T("Model detail"), null, Choices.ModelLod, "model_lod");
            SwitchRow(EngineSwitches.All.Single(s => s.Env == "BB_ANISO"), SymbolRegular.Image24);
            foreach (var effect in Choices.Effects.Where(e => DetailEffects.Contains(e.Key)))
                IniToggle(SymbolRegular.Color24, effect.Title, effect.Hint, effect.Key);

            // ==================== Effects ====================
            // 0.4.4 beta 27b: from the Look page, which went (the tester: they belong with the detail).
            Group(L.T("Effects"));
            Body.Children.Add(Text(L.T("Patches to the game, applied when it starts."), "Faint"));
            TextBlock? blurHint = null;
            foreach (var effect in Choices.Effects.Where(e => LookEffects.Contains(e.Key)))
            {
                var motionBlur = effect.Key == "effect_motion_blur";
                IniToggle(SymbolRegular.Color24, effect.Title, effect.Hint, effect.Key, changed: motionBlur ? ShowMotionBlur : null);
                if (!motionBlur) continue;
                blurHint = LastRow.Hint;
                // Beside the blur it is about, under it (was among the engine switches).
                SwitchRow(EngineSwitches.All.Single(s => s.Env == "BB_UPSCALER_NO_BLUR"), SymbolRegular.Color24);
                Nest();
            }
            _motionBlurHint = blurHint!;

            Ws.PortChanged += UpdateUpscaler;
            // AMD's list arrives on a worker thread: shown again here, and kept for the next start.
            void Arrived() => Dispatcher.BeginInvoke(() =>
            {
                AmdFsrVersions.Remember();
                ShowFsrVersions();
            });
            Loaded += (_, _) => AmdFsrVersions.Changed += Arrived;
            Unloaded += (_, _) => AmdFsrVersions.Changed -= Arrived;
            UpdateUpscaler();
        }

        public override void OnShown()
        {
            AmdFsrVersions.Refresh(); // once a session, in the background
            UpdateUpscaler(); // the output (Display) may have changed
        }

        /// <summary>The Anti-aliasing choice config.ini holds: TAA or Off from the upscaler, the game's own
        /// from effect_game_aa. With an upscaler, what Upscaler None goes back to.</summary>
        private static string AntiAliasingNow() => Ini.Get("upscaler") switch
        {
            "taa" => "taa",
            "off" => Ini.Flag("effect_game_aa") ? "game" : "off",
            _ => Ini.Flag("effect_game_aa") ? "game" : "taa",
        };

        private static double CustomScale() => Math.Clamp(ParseDouble(Ini.Get("render_scale", "1.30"), 1.3), 1.0, 3.0);

        /// <summary>The quality list (Auto with what it picks for the output chosen on Display; Native AA as
        /// DLAA with DLSS) and the slider: the share and the size the game renders at, as the engine works
        /// it out (Choices.RenderSize), movable with Custom only.</summary>
        private void ShowQuality()
        {
            var (ow, oh) = Choices.OutputSize(Ini.Get("output_res", "1920x1080"));
            var live = Ini.Get("live_resolution", "0") == "1";
            var dlss = Ini.Get("upscaler") == "dlss";
            var saved = Ini.Get("preset", "auto");
            string Name(int preset) => preset == 0 && dlss ? "DLAA" : Choices.Presets[preset + 1].Label;
            var preset = Choices.ResolvePreset(saved, oh);
            var custom = preset == Choices.PresetCustom;
            var scale = custom ? CustomScale() : Choices.PresetScales[preset];
            var (w, h) = Choices.RenderSize(ow, oh, scale, live);
            var percent = ow > 0 ? (int)Math.Round(100.0 * w / ow, MidpointRounding.AwayFromZero) : 100;
            _syncing = true;
            try
            {
                for (var i = 0; i < Choices.Presets.Length; i++)
                    ((ComboBoxItem)_quality.Items[i]).Content = i == 0
                        ? $"{Choices.Presets[0].Label} ({Name(Choices.AutoPreset(oh))})"
                        : Name(i - 1);
                // Selected again: the closed box shows the selected entry's text as it was when picked.
                _quality.SelectedIndex = -1;
                _quality.SelectedIndex = Math.Max(0, Choices.IndexOf(Choices.Presets, saved));
                _scale.Minimum = Choices.RenderPercentMin(ow, oh, live);
                _scale.Value = Math.Clamp(custom ? Math.Round(100 / scale) : percent, _scale.Minimum, 100);
                _scaleText.Text = $"{percent}% — {w} × {h}";
                // Locked, not greyed: what it shows applies.
                _scale.IsHitTestVisible = custom;
                _scale.Focusable = custom;
                SetHint(_scaleHint, custom ? null : L.T("Choose Custom to set your own"));
            }
            finally { _syncing = false; }
        }

        /// <summary>FSR version: Automatic (with the newest AMD offers here) and each version AMD lists
        /// (AmdFsrVersions); a saved one AMD does not offer here runs Automatic, and says so.</summary>
        private void ShowFsrVersions()
        {
            var offered = AmdFsrVersions.Known;
            var saved = Ini.Get("fsr_version", "auto");
            var match = AmdFsrVersions.Match(offered, saved);
            string Shown(string name) => "FSR " + AmdFsrVersions.Key(name) + (AmdFsrVersions.FromDriver(name) ? " " + L.T("(driver)") : "");
            var newest = AmdFsrVersions.Newest(offered);
            _syncing = true;
            try
            {
                _fsr.Items.Clear();
                _fsrValues = new List<string> { "auto" };
                _fsr.Items.Add(new ComboBoxItem { Content = newest == null ? L.T("Automatic") : $"{L.T("Automatic")} ({Shown(newest)})" });
                foreach (var name in offered)
                {
                    _fsr.Items.Add(new ComboBoxItem { Content = Shown(name) });
                    _fsrValues.Add(AmdFsrVersions.Key(name));
                }
                _fsr.SelectedIndex = match + 1; // none: Automatic
            }
            finally { _syncing = false; }
            _fsrRow.Choices = string.Join(" · ", _fsr.Items.Cast<ComboBoxItem>().Select(i => i.Content as string));
            SetHint(_fsrHint, match < 0 && saved is not ("auto" or "") && offered.Count > 0
                ? L.F($"FSR {saved} is not offered on this GPU: Automatic runs")
                : null);
        }

        private void UpdateUpscaler()
        {
            var value = Ini.Get("upscaler");
            var upscaling = value is "fsr" or "dlss";
            var ngx = Paths.DlssRuntime;
            // A note only for what needs action, or what runs (DLSS's version).
            SetHint(_upscalerHint, value switch
            {
                "fsr" when !Paths.AmdFsrPresent => L.T("AMD's FSR files are missing from the engine folder (engine\\amd-fsr): the game runs TAA instead"),
                "dlss" when ngx == null => L.T("DLSS files are missing: the game uses AMD FSR"),
                "dlss" => "DLSS " + Paths.DlssVersion(ngx!),
                _ => null,
            });
            // The anti-aliasing an upscaler does itself: greyed, nothing chosen, kept for Upscaler None.
            _syncing = true;
            try
            {
                if (upscaling) Unpick(_aa);
                else Pick(_aa, Math.Max(0, Choices.IndexOf(Choices.AntiAliasing, AntiAliasingNow())));
            }
            finally { _syncing = false; }
            _aaRow.IsEnabled = !upscaling;
            if (upscaling) SetHint(_aaHint, L.F($"{(value == "dlss" ? "DLSS" : "AMD FSR")} anti-aliases (Upscaler below)"));
            ShowQuality();
            var upscalerRows = upscaling ? Visibility.Visible : Visibility.Collapsed;
            _qualityRow.Visibility = upscalerRows;
            _scaleRow.Visibility = upscalerRows;
            // RCAS runs after an upscaler or TAA only; character motion vectors too (ObjectMotion).
            var temporal = value != "off";
            _sharpenCard.IsEnabled = temporal;
            _sharpnessCard.IsEnabled = temporal && Ini.Flag("sharpen"); // as in game
            _mipBiasCard.IsEnabled = temporal;
            _motionVectors.IsEnabled = temporal;
            _fsrRow.Visibility = value == "fsr" ? Visibility.Visible : Visibility.Collapsed;
            if (value == "fsr") ShowFsrVersions();
            var dlss = value == "dlss" ? Visibility.Visible : Visibility.Collapsed;
            _dlssModel.Visibility = dlss;
            _dlssTextures.Visibility = dlss;
            _dlssExposure.Visibility = dlss;
            if (value == "dlss")
            {
                var (_, oh) = Choices.OutputSize(Ini.Get("output_res", "1920x1080"));
                var model = Choices.DlssAutoModel(Choices.ResolvePreset(Ini.Get("preset", "auto"), oh), CustomScale());
                SetHint(_dlssHint, Ini.Get("dlss_preset", "auto") == "auto" ? L.F($"Auto: model {model} at this quality") : null);
            }
            ShowMotionBlur();
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
