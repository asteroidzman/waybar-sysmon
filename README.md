# waybar-sysmon

<p align="center"><img src="assets/icon.png" width="128" alt="waybar-sysmon icon"></p>

A [waybar](https://github.com/Alexays/Waybar) **CFFI plugin** showing **CPU and
RAM usage** in one pill, with a click-through popover for per-core CPU and a
memory/swap breakdown. Reads `/proc` directly — no external tools.

## Features

- Bar pill: ` cpu%`  ` ram%`, colour-graded at warn/danger thresholds
  (CPU 60/80 %, RAM 75/90 %).
- **Click → popover:** total + per-core CPU bars, RAM used/total, swap.
- Refresh interval configurable (default 3s).

## Build & install

Arch Linux: `yay -S waybar-sysmon` (AUR).

Requires `gtk3`, `glib2` (+dev headers) and a C compiler.

```sh
make
make install                 # → ~/.local/lib/waybar/libsysmon.so
```

## waybar config

```jsonc
"modules-right": ["cffi/sysmon"],

"cffi/sysmon": {
    "module_path": "/home/YOU/.local/lib/waybar/libsysmon.so",
    "interval": 3
}
```

| key | default | meaning |
|-----|---------|---------|
| `module_path` | *(required)* | path to `libsysmon.so` |
| `interval` | 3 | refresh seconds |
| `icon-size` | 26 | bar icon pixel size |
| `icon-dir` | `$XDG_DATA_HOME/waybar-sysmon` | dir holding `cpu.svg` / `ram.svg` (installed by `make install`) |

The bar shows crafted image icons (a CPU chip and a memory module); the CPU/RAM
warn/danger thresholds colour the **percentage text** rather than the icons.

## style.css

Bar: `#sysmon` with `.sm-cpu-icon` / `.sm-cpu` / `.sm-ram-icon` / `.sm-ram`
(icons gain `.warn` / `.danger` past the thresholds). Popover: `.sm-pop`,
`.sm-head`, `.sm-lbl`, `.sm-bar` (`.warn`/`.danger`), `.sm-val`.

## License

MIT
