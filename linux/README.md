# Classic Linux client and Wine fallback

The experimental Linux client is native C17 with Xlib, Xt and Athena Widgets
(Xaw), using the Windows workbench palette. It runs under XWayland on a Wayland
desktop. Square controls, bitmap fonts, paper-colored text and simple dividers
are deliberate. No browser engine or Windows runtime is used by this build.

Xt/Xaw are old X11 APIs that still ship in Fedora. They are a reasonable fit for
this small classic client, with older accessibility, text layout and scaling
behavior. They are not a native Wayland toolkit. GTK 2 is not required.

## Run

From the repository root:

```sh
linux/scripts/run.sh          # opens server manager
linux/scripts/run.sh --demo   # offline visual preview; no saved settings
linux/scripts/run-wine.sh     # Windows portable release in Wine
```

The two clients have separate settings. Enter your Hermes root URL, such as
`https://server:port`, and login. Passwords are never saved. HTTP must be enabled
explicitly and resolve exclusively to private or loopback addresses. HTTPS keeps
normal certificate verification. Private HTTP connections pin the resolved IP.

Server offers saved profiles. New chat creates an independent draft.
Ctrl+Enter sends; Ctrl+N starts a new chat; Ctrl+R refreshes or opens Server.
Select and copy text with normal Xaw text bindings; middle click pastes the X11
primary selection. Paned grips resize columns and the composer.

Model / Folder reads the configured server catalog. Enter model/provider IDs;
a blank model in a new draft uses the server default. Existing chats resume before
changing models. A server working folder can be selected for a new draft; loaded
chats retain their own folder. The right pane reads server resources and activity.
Answer Hermes request opens a native approval or clarification panel. Approvals
support deny and, when advertised, allow once. Unsupported requests return errors.

## Scope

Implemented: login, saved server profiles, chat listing/resume/create, JSON-RPC
WebSocket streaming, stop, distinct drafts, model selection, server working folder,
server monitor, activity, approval/clarification replies and explicit reconnect.
Drafts remain until prompt acknowledgement. Failed/uncertain writes are not retried.
Server tasks remain running when the client closes.

This is a first Linux version, not Windows feature parity. It does not yet provide
attachments/image previews, rich Markdown styling, folder trees/search, chat
rename/delete/export, tray/cron notifications, provider/OAuth administration,
reasoning-effort selection or server-wide model settings. Markdown source is
shown as plain text. The full Windows version is the fallback for those features.

## Encrypted local settings

Linux settings use `$XDG_DATA_HOME/lcb-hermes/private-linux.bin`, normally
`~/.local/share/lcb-hermes/private-linux.bin`. AES-256-GCM authenticates and encrypts
saved profiles, cookies and draft state. A random encryption key is held in
Secret Service under `org.lcb.hermes.linux`, purpose `storage-key`. Directories
are private and writes replace a mode-0600 file atomically. Windows DPAPI files
are not imported or overwritten.

An unlocked Secret Service wallet is needed for persistent settings. When saving
fails, the status area reports it and settings remain in memory for that process;
there is no plaintext fallback. Existing settings that cannot be authenticated
are preserved and not overwritten. Fix wallet access before relying on drafts
surviving a restart.

## Build on Fedora

```sh
sudo dnf install gcc cmake pkgconf-pkg-config libXaw-devel libsoup3-devel \
  libsecret-devel openssl-devel xorg-x11-fonts-misc xorg-x11-server-Xvfb
# XWayland is also needed on a Wayland desktop.
linux/scripts/build.sh
```

The script configures MinSizeRel and runs four regression suites. The UI and
transport suites use a local fixture, never a real server. UI tests use an
isolated Xvfb display and do not write user settings. Building just the reusable
core is supported with `-DLCB_BUILD_LINUX=OFF`. For a system without test tools,
configure `-DLCB_BUILD_TESTS=OFF` and build normally.

The 2026-10-08 Fedora 44 build's stripped executable is about 81 KiB, dynamically
linked to system libraries. An idle offline preview used about 14 MiB RSS;
the Windows EXE under Wine used about 44 MiB RSS for its process, excluding Wine
helper processes. These are development observations, not equivalent-workload
benchmarks or total installation sizes. Shared libraries remain required.

## Windows portable release in Wine

```sh
sudo dnf install wine-core wine-common
python3 linux/scripts/fetch-portable.py
linux/scripts/run-wine.sh
```

The downloader selects the newest published Windows portable release, including
prereleases, and verifies its ZIP against that release's SHA256SUMS.txt. The tested
release is 0.50-pre1. Wine uses a dedicated prefix at
`~/.local/share/lcb-hermes-wine`; set WINEPREFIX to use another profile.
The launcher disables optional Mono/Gecko loads; neither is required by this app.

Fedora's wine-common metadata is needed even when choosing a minimal wine-core
install. Omitting it caused first-run setup to loop looking for winmd files;
installing wine-common supplies the missing files. The initialization processes
that were stopped before discovering this issue were not Hermes application
crashes. Avoid running one Wine prefix on two X displays at the same time.

The Wine fallback and native client should both be checked against your real
Hermes server before treating them as production clients. Local fixtures validate
the protocol flow, not every deployed server variation.

## References

- [Athena widget documentation](https://www.x.org/releases/current/doc/libXaw/libXaw.html)
- [Xt documentation](https://www.x.org/releases/current/doc/libXt/intrinsics.html)
- [libsoup](https://libsoup.gnome.org/libsoup-3.0/)
- [Secret Service library](https://gnome.pages.gitlab.gnome.org/libsecret/)
- [Wine API reference](https://source.winehq.org/WineAPI/winhttp.html)
