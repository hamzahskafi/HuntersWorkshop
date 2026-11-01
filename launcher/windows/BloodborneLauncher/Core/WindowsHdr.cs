using System.Runtime.InteropServices;

namespace BloodborneLauncher.Core
{
    /// <summary>
    /// Windows' HDR switch per display (Settings › Display › Use HDR), through the display
    /// configuration API. Some drivers switch a display out of HDR when a game covers the screen
    /// with an SDR image and leave it off after the game exits; the launcher notes which displays
    /// had HDR on before the game started and turns it back on afterwards.
    /// </summary>
    public static class WindowsHdr
    {
        /// <summary>One active display. Source is its Windows device name (\\.\DISPLAY1), as the launcher's
        /// Monitor setting stores it; empty when Windows does not say.</summary>
        public sealed record Display(uint AdapterLow, int AdapterHigh, uint Target, bool Supported, bool On, string Source = "");

        /// <summary>The display the game opens on (Display › Monitor's device, or the Windows primary
        /// monitor's when empty); null when it cannot be told (older Windows, a remote session, an
        /// absent monitor): HDR's switch is then left as it is. 0.4.4 beta 26b.</summary>
        public static Display? ForMonitor(string device)
        {
            if (string.IsNullOrEmpty(device))
                device = WindowsMonitors.All().FirstOrDefault(m => m.Primary)?.Device ?? "";
            if (device.Length == 0) return null;
            return Displays().FirstOrDefault(d => string.Equals(d.Source, device, StringComparison.OrdinalIgnoreCase));
        }

        /// <summary>Whether any active display has HDR on.</summary>
        public static bool AnyOn() => Displays().Any(d => d.On);

        /// <summary>Whether any active display can show HDR.</summary>
        public static bool AnySupported() => Displays().Any(d => d.Supported);

        public static List<Display> Displays()
        {
            var list = new List<Display>();
#if AVALONIA
            // Linux: HDR is the desktop's (KDE Plasma, GNOME, gamescope): nothing to read or turn back on here.
            if (!OperatingSystem.IsWindows()) return list;
#endif
            try
            {
                if (GetDisplayConfigBufferSizes(QdcOnlyActivePaths, out var paths, out var modes) != 0) return list;
                var pathInfo = new PathInfo[paths];
                var modeInfo = new ModeInfo[modes];
                if (QueryDisplayConfig(QdcOnlyActivePaths, ref paths, pathInfo, ref modes, modeInfo, IntPtr.Zero) != 0) return list;
                foreach (var path in pathInfo.Take((int)paths))
                {
                    var target = path.TargetAdapter;
                    var source = new SourceName { Header = Header(InfoSourceName, Marshal.SizeOf<SourceName>(), path.SourceAdapter, path.SourceId) };
                    var gdi = DisplayConfigGetDeviceInfo(ref source) == 0 ? source.Gdi ?? "" : "";
                    var info2 = new AdvancedColorInfo2 { Header = Header(InfoAdvancedColor2, Marshal.SizeOf<AdvancedColorInfo2>(), target, path.TargetId) };
                    if (DisplayConfigGetDeviceInfo(ref info2) == 0)
                    {
                        // Windows 11 24H2 and later: HDR separate from Auto Color Management.
                        list.Add(new Display(target.Low, target.High, path.TargetId,
                            (info2.Value & 0x10) != 0, info2.ActiveColorMode == 2, gdi));
                        continue;
                    }
                    var info = new AdvancedColorInfo { Header = Header(InfoAdvancedColor, Marshal.SizeOf<AdvancedColorInfo>(), target, path.TargetId) };
                    if (DisplayConfigGetDeviceInfo(ref info) == 0)
                        list.Add(new Display(target.Low, target.High, path.TargetId,
                            (info.Value & 1) != 0, (info.Value & 2) != 0 && (info.Value & 4) == 0, gdi));
                }
            }
            catch
            {
                // Not available (older Windows, remote session): nothing to report.
            }
            return list;
        }

        /// <summary>Turns HDR back on for displays that had it before and have it off now.
        /// Returns how many were turned back on.</summary>
        public static int Restore(IReadOnlyList<Display> before)
        {
            var count = 0;
            var now = Displays();
            foreach (var was in before.Where(d => d.On))
            {
                var current = now.FirstOrDefault(d => d.AdapterLow == was.AdapterLow && d.AdapterHigh == was.AdapterHigh && d.Target == was.Target);
                if (current == null || current.On) continue;
                var adapter = new Luid { Low = was.AdapterLow, High = was.AdapterHigh };
                var hdr = new SetState { Header = Header(SetHdrState, Marshal.SizeOf<SetState>(), adapter, was.Target), Value = 1 };
                if (DisplayConfigSetDeviceInfo(ref hdr) == 0) { count++; continue; }
                var color = new SetState { Header = Header(SetAdvancedColorState, Marshal.SizeOf<SetState>(), adapter, was.Target), Value = 1 };
                if (DisplayConfigSetDeviceInfo(ref color) == 0) count++;
            }
            return count;
        }

        private const uint QdcOnlyActivePaths = 2;
        private const uint InfoSourceName = 1, InfoAdvancedColor = 9, SetAdvancedColorState = 10, InfoAdvancedColor2 = 15, SetHdrState = 16;

        private static DeviceInfoHeader Header(uint type, int size, Luid adapter, uint id) =>
            new() { Type = type, Size = (uint)size, Adapter = adapter, Id = id };

        [StructLayout(LayoutKind.Sequential)]
        private struct Luid { public uint Low; public int High; }

        [StructLayout(LayoutKind.Sequential)]
        private struct PathInfo
        {
            public Luid SourceAdapter; public uint SourceId, SourceModeIndex, SourceFlags;
            public Luid TargetAdapter; public uint TargetId, TargetModeIndex, OutputTechnology, Rotation, Scaling;
            public uint RefreshNumerator, RefreshDenominator, ScanLineOrdering; public int TargetAvailable; public uint TargetFlags;
            public uint Flags;
        }

        [StructLayout(LayoutKind.Sequential, Size = 64)]
        private struct ModeInfo { public uint InfoType, Id; public Luid Adapter; }

        [StructLayout(LayoutKind.Sequential)]
        private struct DeviceInfoHeader { public uint Type, Size; public Luid Adapter; public uint Id; }

        [StructLayout(LayoutKind.Sequential)]
        private struct AdvancedColorInfo { public DeviceInfoHeader Header; public uint Value, ColorEncoding, BitsPerColorChannel; }

        [StructLayout(LayoutKind.Sequential)]
        private struct AdvancedColorInfo2 { public DeviceInfoHeader Header; public uint Value, ColorEncoding, BitsPerColorChannel, ActiveColorMode; }

        [StructLayout(LayoutKind.Sequential)]
        private struct SetState { public DeviceInfoHeader Header; public uint Value; }

        /// <summary>DISPLAYCONFIG_SOURCE_DEVICE_NAME: the source's GDI device name (\\.\DISPLAY1).</summary>
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct SourceName
        {
            public DeviceInfoHeader Header;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Gdi;
        }

        [DllImport("user32.dll")]
        private static extern int GetDisplayConfigBufferSizes(uint flags, out uint paths, out uint modes);

        [DllImport("user32.dll")]
        private static extern int QueryDisplayConfig(uint flags, ref uint paths, [Out] PathInfo[] pathInfo, ref uint modes,
            [Out] ModeInfo[] modeInfo, IntPtr topology);

        [DllImport("user32.dll", EntryPoint = "DisplayConfigGetDeviceInfo")]
        private static extern int DisplayConfigGetDeviceInfo(ref AdvancedColorInfo info);

        [DllImport("user32.dll", EntryPoint = "DisplayConfigGetDeviceInfo")]
        private static extern int DisplayConfigGetDeviceInfo(ref AdvancedColorInfo2 info);

        [DllImport("user32.dll", EntryPoint = "DisplayConfigGetDeviceInfo")]
        private static extern int DisplayConfigGetDeviceInfo(ref SourceName info);

        [DllImport("user32.dll", EntryPoint = "DisplayConfigSetDeviceInfo")]
        private static extern int DisplayConfigSetDeviceInfo(ref SetState info);
    }
}
