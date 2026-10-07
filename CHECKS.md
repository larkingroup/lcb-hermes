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

0.4.6 PC prerelease / Windows x64, Zig 0.16 C compiler; warnings treated as errors.

- Seven local C suites: protocol/replay, Markdown, endpoints/DPAPI, native chat/menu
  regressions, resize/streaming, model/server controls, and chat lifecycle/attachments.
- Recent-first session listing, canonical/runtime ID reconciliation, saved dates,
  completion color/order, attachment tray/removal and uncertain-send draft retention.
- Read-only 0.21.5 audit: 28 distinct saved sessions and 28 unique local references;
  repeated numbered titles were distinct server history, with no deletion performed.
- Live NAS: native login, WebSocket streaming, browser navigation, saved-chat resume, stats.
- Live desktop controls: empty defaults, image upload/preview/reply, separate background chat,
  server-menu return, image metadata after resume, deletion of its own test chat.
- Android after folder move: unit tests, release lint, signed APK and AAB build passed.

Historical desktop checks are kept locally; current contract checks are tracked
under `desktop/tests`.

0.50-pre1 PC prerelease / 2026-10-07:

- Nine passing suites: the seven local checks plus two tracked native contract suites.
- Unfiled resume/fallback, loaded workspace changes, old-cache migration, cron
  baseline/deduplication, tray restore/Explorer restart/exit, server-model scope.
- Read-only 0.21.5 check: 26 chats, two tree groups, default model and cron endpoint.
- Server-default writes and notifications use captured transports/Shell calls in
  regression checks; no live model changes or real cron execution were performed.
- Native settings checks cover credential payloads and clearing, endpoint edits,
  device/PKCE flow handling, cancellation and late responses, panel generations,
  targeted config patches, title-model scope, and container update gating.
- Live authenticated reads confirm config, credential metadata, OAuth catalog,
  custom endpoints and managed-runtime update status. No provider login or key was
  changed. Actual provider authorization still needs an interactive account login.
- Live targeted workaround: disable model title upgrades on 0.21.5 while preserving
  instant titles. Readback confirms `model_upgrade_enabled: false`, `enabled: true`.
  Chat model and reasoning settings were not changed.
- One live test prompt: expected reply received, one title event, zero auxiliary
  title warnings, explicit default folder and Chats grouping. Deletion of this
  test's own conversation was confirmed; existing history was left alone.
