# gluewin

A very light X11 window manager that looks and works like Windows 11, for people moving from Windows to Linux.
Plain C on Xlib/Xft/XRender: no toolkit, no compositor. About 160 KB on disk and ~14 MB RAM.

- Centered taskbar with Start, Search, Task View, pinned apps, tray, clock and Show desktop
- Start menu with Pinned apps, All apps and search; power menu
- Quick settings: Wi-Fi, Bluetooth, dark mode, brightness, volume, battery
- Rounded windows with Windows-style title buttons, Snap (drag to top or sides), Alt+Tab
- Virtual desktops through Task View, like Windows
- Optional automatic tiling
- `gluewin-settings`: a Windows 11 style Settings app (display, sound, battery, about, network, Bluetooth, personalization, apps, updates)

## Keys

| Key | Action |
|-----|--------|
| Win | Start |
| Win+Tab | Task View (desktops) |
| Win+1…9 | Go to desktop 1…9 |
| Win+Shift+1…9 | Move window to desktop 1…9 |
| Win+Ctrl+Left/Right | Previous / next desktop |
| Win+T | Tiling on/off |
| Win+F | Float / tile the focused window |
| Win+Left/Right | Snap left/right (swap position when tiling) |
| Win+Up/Down | Maximize / restore, minimize |
| Win+D, Win+M | Show desktop, minimize all |
| Win+E, Win+Enter | Files, terminal |
| Win+A, Win+I, Win+L | Quick settings, Settings, lock |
| Alt+Tab, Alt+F4 | Switch windows, close window |

## Config

`~/.config/gluewin/config` (defaults in `/etc/gluewin/config`); the Settings app writes it for you.

```
theme=light          # light | dark
accent=#0067C0
align=center         # center | left
bar=bottom           # bottom | top
tiling=0             # 1 = new desktops tile windows
wallpaper=/path/to/image
pinned=files.desktop browser.desktop terminal.desktop
```

## Build

Needs libX11, libXft, libXrender, libXext, libXrandr and libpng.

```
make
sudo make PREFIX=/usr install
```

Pick "Gluewin" in your display manager, or run `gluewin-session`.
Optional helpers: `xwallpaper`, `brightnessctl`, `wireplumber` or `pactl`, NetworkManager, bluez, `pavucontrol`, `arandr`.
