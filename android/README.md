# android

Android 8+; targets Android 16. No ads or purchases.
Classic beige workbench, chat history, attachments, voice input, requests,
notifications, server stats, and export from the Chat menu.

Enter your Hermes URL and login. Private HTTP needs the checkbox.
First install opens server setup. Named server profiles and dashboard cookies are
encrypted with an Android Keystore key. Removing a profile clears its login while
retaining drafts and cached history for recovery when the server is added again.

The model picker searches authenticated providers and changes the current chat's
model and reasoning effort through Hermes. New chats inherit server defaults unless
you choose otherwise. Workspaces register existing server folders using the projects
RPC; folder choices are local to this server profile and go into `session.create`.

The chat list shows saved creation dates and source, groups server folders, and
marks background completions blue until opened. Drag its divider on wider screens;
the phone drawer has an expand control. Hermes activity expands on phones, and
screens at least 900 dp wide also show a persistent server monitor.

Attachments remain above the composer until the prompt acknowledgement. Saved
messages use compact attachment references. Streaming updates preserve message
views and the reader's scroll position. [Gateway contracts](../desktop/docs/hermes-protocol.md)
describe the shared protocol.

From this folder: `gradlew assembleDebug` with an Android SDK and JDK 17+.
Windows setup: `python scripts/bootstrap-toolchain.py`, then `scripts/build.ps1`.
Keep `.signing` private and backed up; it owns app updates.
