using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using BloodborneLauncher.Core;
using Wpf.Ui.Controls;
using TextBlock = System.Windows.Controls.TextBlock;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// What the game renders (0.4.4 beta 27b, after the tester's review of 26b; the Look page went). The
    /// upscaler and the resolution it renders at, the anti-aliasing used without one, sharpening, with
    /// the fine settings folded away; then the detail that costs frames, and the game's effects, a
    /// matter of taste. A row that only matters for another's choice sits under it, indented (Nest).
    /// Frame generation is not offered (community-fixes-plan.md, Decisions).
    /// </summary>
    public sealed class GraphicsPage : LauncherPage
    {
        /// <summary>The game's effects in Detail: what costs frames or sets detail.</summary>
        public static readonly string[] DetailEffects = { "effect_dynamic_shadows", "effect_ssao", "effect_ssr" };
        /// <summary>The game's effects in Effects: the lens and camera effects people turn off or on by taste.</summary>
        public static readonly string[] LookEffects = { "effect_dof", "effect_motion_blur", "effect_chromatic_aberration" };

        private readonly TextBlock _upscalerHint, _renderResHint, _aaHint, _motionBlurHint;
        private readonly ComboBox _renderRes = new() { Width = 300, VerticalAlignment = VerticalAlignment.Center };
        private readonly FrameworkElement _aa;
        private bool _relabeling, _syncingAa;
        private readonly SettingRow _renderResRow, _renderScaleCard, _aaRow, _sharpenCard, _sharpnessCard, _mipBiasCard, _motionVectors;
        private readonly SettingRow _dlssModel, _dlssTextures, _dlssExposure, _fsrVersion;

        public GraphicsPage() : base(L.T("Graphics"), "")
        {
            // Sets the Display page's resolution and frame rate and this page's upscaler and preset.
            Add(Card(SymbolRegular.Gauge24, L.T("Best settings for this PC"),
                L.T("The output resolution, frame rate, upscaler and render resolution that suit this PC; you see each change before it is applied") +
                "\n\n" + Hardware.Detect().Summary,
                SmallButton(L.T("Recommend…"), SymbolRegular.Sparkle24, (_, _) => MainWindow.Instance!.OfferBestSettings(firstRun: false)), out _));

            Group(L.T("Upscaling and anti-aliasing"));
            // 0.4.4 beta 27b: the upscalers, or None, where Anti-aliasing below decides (upscaler=taa or
            // off). One FSR, AMD's, through AMD's FSR SDK (engine\amd-fsr), which picks the version.
            ComboRow(SymbolRegular.Sparkle24, L.T("Upscaler"),
                L.T("Renders the game below the output resolution and fills it in, anti-aliasing as it goes. None: the game renders at the output, with the anti-aliasing below"),
                Choices.FrameUpscalers, () => Ini.Get("upscaler") is "fsr" or "dlss" ? Ini.Get("upscaler") : "none",
                v => Ini.Set("upscaler", v != "none" ? v : Ini.Flag("effect_game_aa") ? "off" : "taa"), changed: UpdateUpscaler);
            _upscalerHint = (TextBlock)LastRow.Tag;
            // AMD's choice of FSR version or FSR 3.1, with AMD FSR only (as in game).
            IniCombo(SymbolRegular.BrainCircuit24, L.T("FSR version"), null, Choices.FsrVersions, "fsr_version");
            _fsrVersion = Nest();
            // DLSS's model, with DLSS only (as the in-game menu's DLSS section).
            IniCombo(SymbolRegular.BrainCircuit24, L.T("DLSS model"),
                L.T("Which NVIDIA model DLSS runs. Also in game (Insert or L3 + R3), where it changes at once"), Choices.DlssModels, "dlss_preset");
            _dlssModel = Nest();

            // 0.4.4 beta 27b: was "Quality preset"; each entry shows the size the game renders at for
            // this output, as the engine works it out (Choices.RenderSize).
            foreach (var _ in Choices.Presets) _renderRes.Items.Add(new ComboBoxItem());
            ComboSearch.Attach(_renderRes);
            _renderResRow = Add(Card(SymbolRegular.ZoomIn24, L.T("Render resolution"),
                L.T("The size the game renders at before the upscaler fills the output: lower is faster and softer. Native AA renders at the output (DLAA with DLSS)"),
                _renderRes, out _renderResHint));
            _renderResRow.Choices = string.Join(" · ", Choices.Presets.Select(c => c.Label));
            _renderRes.SelectionChanged += (_, _) =>
            {
                if (_relabeling || _renderRes.SelectedIndex < 0) return;
                Ini.Set("preset", _renderRes.SelectedIndex.ToString(CultureInfo.InvariantCulture));
                Ws.SaveSoon();
                UpdateUpscaler();
            };
            // The Custom preset's ratio (render_scale), the engine's PresetScale: 1.0 renders at the
            // output (DLAA with DLSS), 3.0 is Ultra Performance.
            SliderRow(SymbolRegular.SlideSize24, L.T("Render scale (Custom preset)"),
                L.T("Output size divided by this is the render size: 1.00 renders at the output, 1.30 a little below, 3.00 like Ultra Performance. DLSS uses the nearest preset's model"),
                1.0, 3.0, 0.05, CustomScale, v =>
                {
                    Ini.Set("render_scale", Fmt(v));
                    RelabelRenderRes();
                }, format: "x0.00", valueWidth: 56);
            _renderScaleCard = Nest();

            // 0.4.4 beta 27b: the anti-aliasing without an upscaler, the game's own pass among them (an
            // upscaler anti-aliases itself; the engine leaves the game's pass out then).
            _aa = ComboRow(SymbolRegular.Color24, L.T("Anti-aliasing"), L.T("How edges are smoothed when no upscaler runs"),
                Choices.AntiAliasing, AntiAliasingNow, v =>
                {
                    if (_syncingAa) return; // shown again from config.ini, not picked
                    Ini.Set("upscaler", v == "taa" ? "taa" : "off");
                    Ini.Set("effect_game_aa", v == "game" ? "1" : "0");
                }, changed: () => { if (!_syncingAa) UpdateUpscaler(); });
            _aaRow = LastRow;
            _aaHint = (TextBlock)_aaRow.Tag;

            IniToggle(SymbolRegular.Eyedropper24, L.T("Sharpening (RCAS)"), L.T("With an upscaler or TAA: turn it up if the picture looks soft"), "sharpen",
                changed: UpdateUpscaler);
            _sharpenCard = LastRow;
            SliderRow(SymbolRegular.SlideSize24, L.T("Sharpening strength"), L.T("Up to 1: the upscaler's own sharpening. Above 1: an extra pass"), 0, 2, 0.05,
                () => ParseDouble(Ini.Get("sharpness"), 0.5), v => Ini.Set("sharpness", Fmt(v)));
            _sharpnessCard = Nest();

            // The fine settings most players leave alone; DLSS's own two with DLSS only.
            var fine = Advanced();
            IniToggle(SymbolRegular.ArrowMove24, L.T("Character motion vectors"), L.T("Less ghosting on characters; costs about 10% FPS"), "object_motion", fine);
            _motionVectors = LastRow;
            SliderRow(SymbolRegular.Image24, L.T("Texture mip bias"),
                L.T("Added to the texture detail the upscaler asks for: below 0 sharper textures (more shimmer), above 0 softer. 0 is the upscaler's own"),
                -2, 1, 0.05, () => ParseDouble(Ini.Get("mip_bias", "0"), 0), v => Ini.Set("mip_bias", Fmt(v)), format: "+0.00;-0.00;0.00", into: fine, valueWidth: 56);
            _mipBiasCard = LastRow;
            ToggleRow(SymbolRegular.Image24, L.T("DLSS sharper textures"),
                L.T("One more mip level of texture detail under DLSS, as NVIDIA's guide asks. Off: the render-to-output ratio only"),
                () => Ini.Get("dlss_mip_bias", "1") != "0", v => Ini.Set("dlss_mip_bias", v ? "1" : "0"), fine);
            _dlssTextures = LastRow;
            ToggleRow(SymbolRegular.BrightnessHigh24, L.T("DLSS auto exposure"),
                L.T("DLSS works out each frame's brightness itself. Only matters at outputs other than 1920x1080: there DLSS gets the finished picture, while at 1920x1080 it gets the game's HDR scene and always does this. Can calm ghosting in dark scenes"),
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
            UpdateUpscaler();
        }

        public override void OnShown() => UpdateUpscaler(); // the output (Display) may have changed

        /// <summary>The Anti-aliasing choice config.ini holds: TAA or Off from the upscaler, the game's own
        /// from effect_game_aa. With an upscaler, what Upscaler None would go back to.</summary>
        private static string AntiAliasingNow() => Ini.Get("upscaler") switch
        {
            "taa" => "taa",
            "off" => Ini.Flag("effect_game_aa") ? "game" : "off",
            _ => Ini.Flag("effect_game_aa") ? "game" : "taa",
        };

        private static double CustomScale() => Math.Clamp(ParseDouble(Ini.Get("render_scale", "1.30"), 1.3), 1.0, 3.0);

        /// <summary>Each Render resolution entry with the size the game renders at for the output chosen
        /// now (Display), and the saved one selected.</summary>
        private void RelabelRenderRes()
        {
            var (ow, oh) = Choices.OutputSize(Ini.Get("output_res", "1920x1080"));
            var live = Ini.Get("live_resolution", "0") == "1";
            var culture = CultureInfo.InvariantCulture;
            _relabeling = true;
            try
            {
                for (var i = 0; i < Choices.Presets.Length; i++)
                {
                    var custom = i >= Choices.PresetScales.Length;
                    var scale = custom ? CustomScale() : Choices.PresetScales[i];
                    var (w, h) = Choices.RenderSize(ow, oh, scale, live);
                    var name = i == 0 ? (Ini.Get("upscaler") == "dlss" ? "DLAA" : L.T("Native AA"))
                        : custom ? L.T("Custom") + " (x" + scale.ToString("0.00", culture) + ")"
                        : Choices.Presets[i].Label;
                    ((ComboBoxItem)_renderRes.Items[i]).Content = $"{name} — {w} × {h}";
                }
                // Selected again: the closed box shows the selected entry's text as it was when picked.
                var preset = int.TryParse(Ini.Get("preset", "0"), NumberStyles.Integer, culture, out var p) ? Math.Clamp(p, 0, Choices.Presets.Length - 1) : 0;
                _renderRes.SelectedIndex = -1;
                _renderRes.SelectedIndex = preset;
            }
            finally { _relabeling = false; }
        }

        private void UpdateUpscaler()
        {
            var value = Ini.Get("upscaler");
            var upscaling = value is "fsr" or "dlss";
            var ngx = Paths.DlssRuntime;
            string? hint = value switch
            {
                // 0.4.4 beta 27: AMD's FSR SDK in engine\amd-fsr; without it the game runs TAA.
                "fsr" => Paths.AmdFsrPresent
                    ? L.T("Through AMD's FSR SDK, which picks the version this GPU runs: FSR 4.1 on Radeon RX 6000 and newer, FSR 3.1 on other GPUs")
                    : L.T("AMD's FSR files are missing from the engine folder (engine\\amd-fsr): the game runs TAA instead"),
                "dlss" => ngx != null
                    ? L.F($"NVIDIA DLSS {Paths.DlssVersion(ngx)}. Needs an NVIDIA GeForce RTX GPU; others use AMD FSR") +
                      (Ini.Get("preset", "0") == "0" ? L.T(". With Native AA this is DLAA: DLSS as anti-aliasing at full resolution") : "")
                    : L.T("DLSS files are missing: the game uses AMD FSR"),
                _ => null,
            };
            SetHint(_upscalerHint, hint);
            RelabelRenderRes();
            _renderResRow.IsEnabled = upscaling;
            SetHint(_renderResHint, upscaling ? null : L.T("With an upscaler only: without one the game renders at the output resolution"));
            _renderScaleCard.Visibility = Ini.Get("preset", "0") == "5" ? Visibility.Visible : Visibility.Collapsed;
            _renderScaleCard.IsEnabled = upscaling;
            // The choice an upscaler leaves to itself: greyed, kept for Upscaler None, and shown as
            // config.ini holds it (an upscaler picked and None again is TAA, or the game's own).
            _syncingAa = true;
            try { Pick(_aa, Math.Max(0, Choices.IndexOf(Choices.AntiAliasing, AntiAliasingNow()))); }
            finally { _syncingAa = false; }
            _aaRow.IsEnabled = !upscaling;
            SetHint(_aaHint, upscaling ? L.T("The upscaler anti-aliases: this applies with Upscaler None") : null);
            if (!upscaling) RefreshAntiAliasingNote();
            // RCAS runs after an upscaler or TAA only.
            var temporal = value != "off";
            _sharpenCard.IsEnabled = temporal;
            _sharpnessCard.IsEnabled = temporal && Ini.Flag("sharpen"); // as in game
            _mipBiasCard.IsEnabled = temporal;
            _motionVectors.IsEnabled = temporal;
            _fsrVersion.Visibility = value == "fsr" ? Visibility.Visible : Visibility.Collapsed;
            var dlss = value == "dlss" ? Visibility.Visible : Visibility.Collapsed;
            _dlssModel.Visibility = dlss;
            _dlssTextures.Visibility = dlss;
            _dlssExposure.Visibility = dlss;
            ShowMotionBlur();
        }

        /// <summary>The chosen anti-aliasing's note, under its row while it applies.</summary>
        private void RefreshAntiAliasingNote()
        {
            var note = Choices.AntiAliasing[Math.Max(0, Choices.IndexOf(Choices.AntiAliasing, AntiAliasingNow()))].Note;
            SetHint(_aaHint, note == null ? null : Brief(note, 110));
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
