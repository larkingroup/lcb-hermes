# Android workbench

The Android client uses the same Hermes gateway contracts as the PC client.
`session.list` is recent-first and session IDs identify history; equal titles stay
distinct. Optional compression `resolved_id` aliases coalesce with their root ID.
`stored_session_id`, `session_key`, and runtime `session_id` have separate roles.
Saved `started_at` and `source` appear in the sidebar; runtime start times never
replace saved creation dates. Background completion order overlays the list until
a subsequent server list supplies authoritative recent order.

`projects.list` and `projects.tree` supply registered and discovered folders.
`projects.create` and `projects.add_folder` register existing server paths.
New-chat folder choices use `cwd`; a different returned folder withholds submission
and retains the draft. Resumed chats use the server's returned folder.

`model.options` supplies authenticated providers. Draft choices become `model`,
`provider`, and `reasoning_effort` in `session.create`. Existing chats use
`config.set`, key `model`, with `--provider`, `--reasoning`, and `--session`.
Cost confirmation and deferred switches follow the server response. Effort choices
follow Hermes' OpenAI model tiers and the reported reasoning capability; the
server owns route-specific clamping. The displayed default comes from `config.get`.

`image.attach` appends to the queue, so retry preparation detaches the same path
before enqueueing it again. Drafts clear only after `prompt.submit` succeeds.
Uncertain sends remain available and are never automatically retried. Attachment
metadata is restored only against matching message text. Conflicting alias drafts
are retained locally, and saved server data is encrypted using Keystore AES-GCM.

Activity bullets reflect status, reasoning availability, and tool events. They do
not invent reasoning text. The authenticated `/api/system/stats` poll runs at most
once every five seconds while the monitor is visible; figures describe the server.

See [the shared protocol notes](../../desktop/docs/hermes-protocol.md) for upstream
Hermes documentation and implementation references.
