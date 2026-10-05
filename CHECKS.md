# checks

Android 16 emulator. Hermes 0.21.5 on TrueNAS.

- Login, browser navigate/type/click/read, streaming, reconnect; prompt sent once.
- Files upload/read; duplicate upload refused.
- Models, approval/clarification UI, encrypted storage, draft rotation.
- Light, dark, landscape. Fourteen connection/parser/stats/draft unit checks. Release lint: no errors.
- 0.1.2: wrong HTTPS recovered on a private server; one model picker after three taps.
- Screenshots allowed. Real reply notification delivered in background and opened chat.
- 0.2.0: inline sessions and native sheets; photo orientation, previews, real image reply, chat reopen.
- 0.2.1: server stats and module icons; back buttons and edge gestures keep the working chat.
- 0.3.0: phone/Fold sidebar, search, separate drafts/images; concurrent replies and reconnect.
- Reply notifications open their own chat. Back gestures and rotation preserve the selection.
- Signed APK and AAB verified. Fold installed via USB.

0.4.0 PC / Windows x64, Zig 0.16 C compiler; warnings treated as errors.

- Four C suites: protocol/replay, Markdown, endpoints/DPAPI, native chat/menu regressions.
- Live NAS: native login, WebSocket streaming, browser navigation, saved-chat resume, stats.
- Live desktop controls: empty defaults, image upload/preview/reply, separate background chat,
  server-menu return, image metadata after resume, deletion of its own test chat.
- Android after folder move: unit tests, release lint, signed APK and AAB build passed.

Run C suites with `ctest --test-dir desktop/build -C Release --output-on-failure`.
Live tests take URL/username/password JSON through stdin; no credentials in tracked fixtures.
