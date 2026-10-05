# Hermes integration notes

Reviewed against the official Hermes source/docs and the existing 0.21.5 server on
2026-10-05. The desktop stays on the authenticated JSON-RPC WebSocket contract it
already uses. The REST session-chat SSE API is a separate surface.

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
No model is hardcoded in the desktop client. A user-selected model/provider is
the only reason `session.create` includes an override.

Authenticated `GET /api/system/stats` supplies the monitor. It is polled every
five seconds while the monitor or connection window is visible, with one stats
request in flight. Readings describe the server host; offline state replaces
the live figures. Responses are scoped to their originating server URL.

## Upstream references

- [Dashboard workspace picker and host statistics](https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/features/web-dashboard.md)
- [Projects RPC implementation](https://github.com/NousResearch/hermes-agent/blob/main/tui_gateway/methods_projects.py)
- [Session create and resume](https://github.com/NousResearch/hermes-agent/blob/main/tui_gateway/methods_session.py)
- [Gateway reasoning callbacks](https://github.com/NousResearch/hermes-agent/blob/main/tui_gateway/agent_callbacks.py)
- [TUI activity words](https://github.com/NousResearch/hermes-agent/blob/main/ui-tui/src/content/verbs.ts)
- [REST/SSE API server](https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/features/api-server.md)
