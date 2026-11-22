# gluewin

A very light X11 window manager that looks and works like Windows 11, for people moving from Windows to Linux.
Plain C on Xlib/Xft/XRender: no toolkit, no compositor. About 160 KB on disk and ~14 MB RAM.

- Centered taskbar with Start, Search, Task View, pinned apps, tray, keyboard language, clock and Show desktop
- Start menu with Pinned apps, All apps and search; power menu
- Quick settings: Wi-Fi, Bluetooth, dark mode, brightness, volume, battery
- Rounded windows with Windows-style title buttons, Snap (drag to top or sides), Alt+Tab
- Virtual desktops through Task View, like Windows; drag a window icon onto another desktop to move it
- Notifications (built-in `org.freedesktop.Notifications` server): toasts and a notification center over a calendar, opened from the clock
- Volume and brightness pop-up when you press the media keys
- Monitors: Win+P asks Duplicate / Extend / Second screen only like Windows, also when you plug a screen in
- Optional automatic tiling
- `gluewin-settings`: a Windows 11 style Settings app (display, sound, battery, about, network, Bluetooth, personalization with wallpaper previews, apps, time & language, updates)

## Keys

| Key | Action |
|-----|--------|
| Win | Start |
| Win+Tab | Task View (desktops) |
| Win+1…9 | Go to desktop 1…9 |
| Win+Shift+1…9 | Take the window to desktop 1…9 |
| Win+Ctrl+Left/Right | Previous / next desktop |
| Win+Space | Next keyboard layout |
| Win+T | Tiling on/off (windows go back to their old size) |
| Win+Q | Close window |
| Win+F, Win+Shift+F | Fullscreen, float / tile the focused window |
| Win+Arrows | Focus the window in that direction |
| Win+Shift+Left/Right | Snap left/right, then to the next monitor (swap places when tiling) |
| Win+Shift+Up/Down | Maximize / restore, minimize |
| Win+N, Win+P | Notifications and calendar, project to a second screen |
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
language=en          # en | ro, display language (sign out to apply to all apps)
keyboard=us,ro       # keyboard layouts, first is the default
wallpaper=/path/to/image
pinned=files.desktop browser.desktop terminal.desktop
```

## Build

Needs libX11, libXft, libXrender, libXext, libXrandr, libpng, libjpeg, libwebp and libdbus.

```
make
sudo make PREFIX=/usr install
```

Pick "Gluewin" in your display manager, or run `gluewin-session`.
Keyboard layouts use `setxkbmap`. Optional helpers: `xwallpaper`, `brightnessctl`, `wireplumber` or `pactl`, `xrandr`, NetworkManager, bluez, `pavucontrol`, `arandr`.
