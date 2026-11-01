using System.IO;
using System.Windows;
using System.Windows.Controls;
using BloodborneLauncher.Core;
using Wpf.Ui.Controls;
using TextBlock = System.Windows.Controls.TextBlock;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// Frames against detail (0.4.4 beta 26b, after the tester's review of 26: "Upscaling" with the
    /// effects elsewhere was the wrong split). Upscaling and anti-aliasing together, the game's own
    /// anti-aliasing included, with DLSS's own model shown for DLSS and its fine settings folded away;
    /// then the detail that costs frames. The look (effects that are a matter of taste, picture tuning)
    /// is on the Look page. Frame generation is not offered (community-fixes-plan.md, Decisions).
    /// </summary>
    public sealed class GraphicsPage : LauncherPage
    {
        /// <summary>The game's effects shown here: what costs frames or sets detail.</summary>
        public static readonly string[] DetailEffects = { "effect_dynamic_shadows", "effect_ssao", "effect_ssr" };

        private readonly TextBlock _upscalerHint;
        private readonly SettingRow _presetCard, _renderScaleCard, _sharpenCard, _sharpnessCard, _mipBiasCard;
        private readonly SettingRow _dlssModel, _dlssTextures, _dlssExposure, _fsrVersion;

        public GraphicsPage() : base(L.T("Graphics"), "")
        {
            // Sets the Display page's resolution and frame rate and this page's upscaler and preset.
            Add(Card(SymbolRegular.Gauge24, L.T("Best settings for this PC"),
                L.T("The output resolution, frame rate, upscaler and quality preset that suit this PC; you see each change before it is applied") +
                "\n\n" + Hardware.Detect().Summary,
                SmallButton(L.T("Recommend…"), SymbolRegular.Sparkle24, (_, _) => MainWindow.Instance!.OfferBestSettings(firstRun: false)), out _));

            Group(L.T("Upscaling and anti-aliasing"));
            // 0.4.4 beta 27: one FSR, AMD's, through AMD's FSR SDK (engine\amd-fsr), which picks the version.
            IniCombo(SymbolRegular.Sparkle24, L.T("Upscaler"), null, Choices.Upscalers, "upscaler", changed: UpdateUpscaler);
            _upscalerHint = (TextBlock)((SettingRow)Body.Children[^1]).Tag;
            // AMD's choice of FSR version or FSR 3.1, with AMD FSR only (as in game).
            IniCombo(SymbolRegular.BrainCircuit24, L.T("FSR version"), null, Choices.FsrVersions, "fsr_version");
            _fsrVersion = (SettingRow)Body.Children[^1];
            // DLSS's model, with DLSS only (as the in-game menu's DLSS section).
            IniCombo(SymbolRegular.BrainCircuit24, L.T("DLSS model"),
                L.T("Which NVIDIA model DLSS runs. Also in game (Insert or L3 + R3), where it changes at once"), Choices.DlssModels, "dlss_preset");
            _dlssModel = (SettingRow)Body.Children[^1];

            IniCombo(SymbolRegular.ZoomIn24, L.T("Quality preset"), L.T("How far below the output resolution the game renders"),
                Choices.Presets, "preset", changed: UpdateUpscaler);
            _presetCard = (SettingRow)Body.Children[^1];
            // The Custom preset's ratio (render_scale), the engine's PresetScale: 1.0 renders at the
            // output (DLAA with DLSS), 3.0 is Ultra Performance.
            SliderRow(SymbolRegular.SlideSize24, L.T("Render scale (Custom preset)"),
                L.T("Output size divided by this is the render size: 1.00 renders at the output, 1.30 a little below, 3.00 like Ultra Performance. FSR 4 and DLSS use the nearest preset's model"),
                1.0, 3.0, 0.05, () => ParseDouble(Ini.Get("render_scale", "1.30"), 1.3), v => Ini.Set("render_scale", Fmt(v)), format: "x0.00", valueWidth: 56);
            _renderScaleCard = (SettingRow)Body.Children[^1];

            IniToggle(SymbolRegular.Eyedropper24, L.T("Sharpening (RCAS)"), L.T("Works with every option above, TAA included: turn it up if the picture looks soft"), "sharpen",
                changed: UpdateUpscaler);
            _sharpenCard = (SettingRow)Body.Children[^1];
            SliderRow(SymbolRegular.SlideSize24, L.T("Sharpening strength"), L.T("Up to 1: the upscaler's own sharpening. Above 1: an extra pass"), 0, 2, 0.05,
                () => ParseDouble(Ini.Get("sharpness"), 0.5), v => Ini.Set("sharpness", Fmt(v)));
            _sharpnessCard = (SettingRow)Body.Children[^1];
            // The game's own anti-aliasing sits with the others (it was among the game effects).
            var gameAa = Choices.Effects.Single(e => e.Key == "effect_game_aa");
            IniToggle(SymbolRegular.Color24, gameAa.Title, L.T("The game's own anti-aliasing pass, applied when the game starts. Usually left off with an upscaler, which smooths edges itself"),
                gameAa.Key);
            IniToggle(SymbolRegular.ArrowMove24, L.T("Character motion vectors"), L.T("Less ghosting on characters; costs about 10% FPS"), "object_motion");

            // The fine settings most players leave alone; DLSS's own two with DLSS only.
            var fine = Advanced();
            SliderRow(SymbolRegular.Image24, L.T("Texture mip bias"),
                L.T("Added to the texture detail the upscaler asks for: below 0 sharper textures (more shimmer), above 0 softer. 0 is the upscaler's own"),
                -2, 1, 0.05, () => ParseDouble(Ini.Get("mip_bias", "0"), 0), v => Ini.Set("mip_bias", Fmt(v)), format: "+0.00;-0.00;0.00", into: fine, valueWidth: 56);
            _mipBiasCard = (SettingRow)fine.Children[^1];
            ToggleRow(SymbolRegular.Image24, L.T("DLSS sharper textures"),
                L.T("One more mip level of texture detail under DLSS, as NVIDIA's guide asks. Off: the render-to-output ratio only"),
                () => Ini.Get("dlss_mip_bias", "1") != "0", v => Ini.Set("dlss_mip_bias", v ? "1" : "0"), fine);
            _dlssTextures = (SettingRow)fine.Children[^1];
            ToggleRow(SymbolRegular.BrightnessHigh24, L.T("DLSS auto exposure"),
                L.T("DLSS works out each frame's brightness itself. Only matters at outputs other than 1920x1080: there DLSS gets the finished picture, while at 1920x1080 it gets the game's HDR scene and always does this. Can calm ghosting in dark scenes"),
                () => Ini.Get("dlss_auto_exposure", "0") == "1", v => Ini.Set("dlss_auto_exposure", v ? "1" : "0"), fine);
            _dlssExposure = (SettingRow)fine.Children[^1];

            // ==================== Detail ====================
            Group(L.T("Detail"));
            Body.Children.Add(Text(L.T("Patches to the game, applied when it starts."), "Faint"));
            IniCombo(SymbolRegular.Cube24, L.T("Model detail"), null, Choices.ModelLod, "model_lod");
            SwitchRow(EngineSwitches.All.Single(s => s.Env == "BB_ANISO"), SymbolRegular.Image24);
            foreach (var effect in Choices.Effects.Where(e => DetailEffects.Contains(e.Key)))
                IniToggle(SymbolRegular.Color24, effect.Title, effect.Hint, effect.Key);

            Ws.PortChanged += UpdateUpscaler;
            UpdateUpscaler();
        }

        public override void OnShown() => UpdateUpscaler();

        private void UpdateUpscaler()
        {
            var value = Ini.Get("upscaler");
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
                "taa" => L.T("Anti-aliasing at the output resolution, without an FSR model. Softer than DLAA or FSR Native AA: add sharpening below"),
                _ => null,
            };
            SetHint(_upscalerHint, hint);
            _presetCard.IsEnabled = value is not ("taa" or "off");
            _renderScaleCard.Visibility = Ini.Get("preset", "0") == "5" ? Visibility.Visible : Visibility.Collapsed;
            _renderScaleCard.IsEnabled = value is not ("taa" or "off");
            _sharpenCard.IsEnabled = value != "off";
            _sharpnessCard.IsEnabled = value != "off" && Ini.Flag("sharpen"); // as in game
            _mipBiasCard.IsEnabled = value != "off";
            _fsrVersion.Visibility = value == "fsr" ? Visibility.Visible : Visibility.Collapsed;
            var dlss = value == "dlss" ? Visibility.Visible : Visibility.Collapsed;
            _dlssModel.Visibility = dlss;
            _dlssTextures.Visibility = dlss;
            _dlssExposure.Visibility = dlss;
        }
    }
}
