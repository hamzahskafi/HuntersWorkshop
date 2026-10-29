# Hunter's Workshop 0.4.1

What changed since 0.4.0, for the release page. The engine work behind each line is in
`docs/community-fixes-plan.md`; every fix taken from another project is credited below and in
`README.md`.

## Fixes

- **Cainhurst's ghosts have their whole dresses.** The game's threads now do their math the way a
  PS4 does, rounding very small numbers to zero; with Windows' way, some ghosts were drawn without
  the lower half of their dresses. Other cloth and physics follow the console the same way. No
  measurable cost to frame rate or CPU. More › Engine › Game fixes › PS4 floating-point mode turns it
  off. (shadPS4 #3568 by StevenMiller123, found by red-prig; via ayozetr)
- **DLSS and FSR:** a frame whose camera numbers are broken can no longer smear the upscaled
  picture, and small objects the GPU animates keep their motion. (Lantern 0.9.38, Awaken)
- **Keyboard:** Enter and Backspace are menu keys only. In the world they used to talk or pick up
  (Enter) and dodge (Backspace). (Lantern 0.9.38, Awaken)

## Updates

- **DLSS 310.9.16** (was 310.9.1). (EthanBlazkowicz)

## Launcher

- **The window fits the screen** at high Windows scaling (a 1080p screen at 150% or 200%), still
  opening fresh and centred each time. (GHusr-y5)
