# privacy

lcb-hermes talks directly to the server you choose. No analytics, ads, or developer relay.
The server and its model providers process your messages and attached files.
Login cookies, per-chat drafts, attachment previews, unread marks, task references, and the last chat
are encrypted with Android Keystore or Windows DPAPI for the current user.
Your password is used to sign in and is not saved. Android backup is disabled.
Image previews use the local files you select; uploads go to your chosen server.
Android handles voice input and sharing when you request them.
Android can forget a server; PC can forget its saved login from the server menu.
PC settings live in `%LOCALAPPDATA%\LCB\hermes\private.bin`.
