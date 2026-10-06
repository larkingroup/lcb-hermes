# Hermes integration notes

Reviewed against the official Hermes source/docs and the existing 0.21.5 server on
2026-10-05. The desktop stays on the authenticated JSON-RPC WebSocket contract it
already uses. The REST session-chat SSE API is a separate surface.

## Session identity and history

`session.list` supplies the human-facing, recent-first list. Its `id` identifies a
saved conversation; newer servers may also return `resolved_id` for the current
compression tip. The client retains both IDs. A live `session_id` is a runtime
handle. Create/resume responses associate it with `stored_session_id`,
`info.stored_session_id` or `session_key`. These are aliases of one sidebar entry,
not reasons to create another conversation. Equal titles alone are never merged.

The 0.21.5 listing returns `started_at` and `source`, but omits `last_active`.
The sidebar uses the server's list order and displays the saved creation time in
local time, with the full date in its tooltip. It does not substitute a resumed
runtime's start time for the original chat date. Completion events move that chat
to the top of its folder and mark an unopened background reply blue.

Client cache references missing from a complete listing are hidden without
deleting saved drafts or server data. A capped listing cannot establish absence.
Identity reconciliation retains drafts, media and pending requests. Distinct
unsent drafts that share a saved ID are preserved as separate local drafts.
Event sequence watermarks reset when the runtime handle or replay epoch changes.

Numbered titles can be real separate sessions or branch titles. The live audit
found 28 distinct saved IDs and 28 unique client entries, including Android and
desktop sessions. None were deleted or combined on the basis of their title.

## Attachments and sending

Authenticated upload returns a server path. Photos and files are staged per chat
in a native tray above the composer, with filenames, thumbnails and removal.
The transcript retains compact sent-file references rather than image padding.

`image.attach` queues images for the next turn; `image.detach` removes a path from
that queue without deleting the uploaded file. Before queuing an unconfirmed
image, the client detaches the same path in that runtime so reconnects cannot
silently queue it twice. Removing a queued image waits for the detach response.
Text and attachments stay in the draft until `prompt.submit` is acknowledged.
An uncertain send retains them and refreshes the session without retrying the
prompt. Chat export is a menu action and does not affect server history.

## Workspaces

`projects.list` reads the server's registered projects and attached folders.
`projects.tree` supplies discovered workspace paths and stored chat metadata, so
the client does not inspect Windows folders to guess a remote chat's workspace.
The current listing requests up to 500 sessions, matching the existing chat list.

`projects.create` takes a name, `folders` and `primary_path`.
`projects.add_folder` takes a project `id`, `path` and optional `is_primary`.
The client does not set the server-wide active project merely because the local
picker changes. Folder selection is remembered per server/login on this PC.

New chats pass `cwd` to `session.create`; resumed chats use their existing
`session_id` and receive authoritative `info.cwd` / `info.project` metadata.
If a create result reports a different folder, the draft is retained and the
prompt is withheld so the user can review it before sending. Unsupported project
methods disable the workspace controls without disabling chat.

Registering a workspace attaches server directories to a Hermes project; it does
not create directories on the server or upload folders from Windows. Moving an
existing chat, deleting projects/folders, and managing Hermes profiles are outside
this UI change.

## Activity and commentary

`thinking.delta`, `reasoning.delta` and `reasoning.available` trigger the Thinking
indicator while the turn is running. `status.update` displays the server's text.
`tool.start`, `tool.generating` and `tool.complete` report actual tool activity.
The activity history is bounded to 40 entries per chat and is transient.

The client does not synthesize model reasoning or request verbose reasoning.
The TUI's rotating words such as pondering are presentation strings from its
`VERBS` list; they are not announcements from the model.

`message.interim` contains actual assistant commentary. If `already_streamed` is
false, the text is appended to the transcript; otherwise the existing streamed
text is retained. The final answer gets its own segment. Replay sequence numbers
prevent duplicates, and late thinking events cannot replace a completed state.

The native chat tree updates existing items in place. Token events update the
transcript/activity, and list-change events are coalesced. Collapsed folder state
and stable row handles survive chat-list refreshes.

## Model and monitor

Read-only `config.get` with `key: provider` reports the configured default model.
No default model is hardcoded in the desktop client. `model.options` supplies the
authenticated provider catalog, pricing and reasoning capability flags. Drafts
save a per-chat model/provider and `reasoning_effort`, passed in `session.create`.
Existing chats use `config.set`, `key: model`, with `--provider`, `--reasoning`
and explicit `--session` scope. Hermes defers a busy chat's switch until its next
turn and handles model-selection confirmation. `config.get`, `key: reasoning`,
with a session ID reads the chat's effort. OpenAI/Codex levels follow upstream
`agent/reasoning_effort.py`; model generations differ in their supported levels.

The server default is separate: the dashboard's `POST /api/model/set`, `scope: main`,
writes its configured model for new sessions. The chat picker does not change it.

Saved servers and cookies share the existing Windows DPAPI store. Names and
login identity select a server; each server/login retains its own drafts. Removing
a server hides its entry and clears its saved cookie while preserving local drafts.
Passwords are never saved.

Authenticated `GET /api/system/stats` supplies the monitor. It is polled every
five seconds while the monitor is visible, with one stats
request in flight. Readings describe the server host; offline state replaces
the live figures. Responses are scoped to their originating server URL.

## Upstream references

- [Dashboard workspace picker and host statistics](https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/features/web-dashboard.md)
- [Projects RPC implementation](https://github.com/NousResearch/hermes-agent/blob/main/tui_gateway/methods_projects.py)
- [Model and reasoning scope](https://github.com/NousResearch/hermes-agent/blob/main/tui_gateway/methods_config_set.py)
- [Reasoning effort vocabulary](https://github.com/NousResearch/hermes-agent/blob/main/agent/reasoning_effort.py)
- [Session create and resume](https://github.com/NousResearch/hermes-agent/blob/main/tui_gateway/methods_session.py)
- [Session history and naming](https://hermes-agent.nousresearch.com/docs/user-guide/sessions/)
- [Session and message contracts](https://github.com/NousResearch/hermes-agent/blob/main/tui_gateway/contracts/common.py)
- [Prompt and attachment RPCs](https://github.com/NousResearch/hermes-agent/blob/main/tui_gateway/methods_prompt.py)
- [Gateway reasoning callbacks](https://github.com/NousResearch/hermes-agent/blob/main/tui_gateway/agent_callbacks.py)
- [TUI activity words](https://github.com/NousResearch/hermes-agent/blob/main/ui-tui/src/content/verbs.ts)
- [REST/SSE API server](https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/features/api-server.md)
