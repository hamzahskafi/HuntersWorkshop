namespace BloodborneLauncher.Core
{
    /// <summary>
    /// The frame rate and V-Sync together (Display › Frame rate; 0.4.4 beta 22). Nothing is
    /// blocked: choosing Uncapped turns V-Sync Off, the way to run it as fast as the PC can, and
    /// going back to a fixed rate turns that Off back to On (latest frame). A choice the player
    /// made stays. A note says when a pairing holds the game to the display.
    /// </summary>
    public static class FrameRateSync
    {
        /// <summary>V-Sync's On (latest frame), the default, and Off (Vulkan's Mailbox and Immediate).</summary>
        public const string Latest = "Mailbox", Off = "Immediate";

        /// <summary>V-Sync after the frame rate went from <paramref name="from"/> to <paramref name="to"/>,
        /// or null to leave it as it is.</summary>
        public static string? After(string from, string to, string sync) =>
            to == "uncap" && from != "uncap" ? Off
            : from == "uncap" && to != "uncap" && sync == Off ? Latest
            : null;

        /// <summary>What the pairing does to the frame rate, or null when nothing: On (every frame) and
        /// Adaptive wait for the display, so above its refresh rate the game runs at the refresh rate.
        /// <paramref name="refreshHz"/>: the display's (Windows reports 59 for 59.94 Hz, so one more
        /// is still the display's rate).</summary>
        public static string? Note(string fps, string sync, int refreshHz)
        {
            if (sync is not ("Fifo" or "FifoRelaxed") || refreshHz < 30) return null;
            if (fps == "uncap") return L.F($"Uncapped is held to your display's {refreshHz} Hz with this setting");
            return int.TryParse(fps, out var rate) && rate > refreshHz + 1
                ? L.F($"The game waits for your {refreshHz} Hz display: {rate} FPS runs at {refreshHz}")
                : null;
        }
    }
}
