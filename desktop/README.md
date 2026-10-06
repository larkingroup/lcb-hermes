# pc

Native C / Win32. Windows 10+ x64. One portable EXE.
Classic controls and embedded icons adapted from [lcb-ai](https://github.com/larkingroup/lcb-ai).

![PC workbench with example data](../branding/pc-workbench.png)

The server manager opens on first launch. Add a name and your configured Hermes URL,
such as `https://server:port`, and login. Check private HTTP if your server uses it.
The computer button switches saved servers. Use Server > Server manager to add or
remove a server. Each server/login keeps its own drafts and workspace selection.

Classic beige interface, separate drafts, images, model selection, requests and export.
Drag the dotted divider to resize the chat list, or use View > Wider/Narrower chat list.
Folders keep their state during replies. The right pane shows resource bars and a
wrapped activity log from Hermes events.
Ctrl+Enter sends; Ctrl+N starts a chat; Ctrl+R reconnects.

Workspaces registers existing folders on the server. Choose a folder for new chats;
resumed chats keep their own working directory. Use server paths, such as
`/srv/projects/research`. Older servers can still chat without the projects API.
The default model comes from server configuration. Click the model button to search
configured providers and choose a model and reasoning effort for the current chat.
OpenAI/Codex choices follow Hermes's supported effort ladder. Changes during a turn
apply on the next turn; new drafts carry their choices in `session.create`.

Settings and login cookies use Windows DPAPI at
`%LOCALAPPDATA%\LCB\hermes\private.bin`. Passwords are not saved.
Closing the client leaves server tasks running.

About credits Nous Research and the Hermes team, with their embedded logo.
[Protocol and upstream references](docs/hermes-protocol.md).

Build with CMake, a C17 compiler and the Windows SDK:

```
cmake -S desktop -B desktop/build
cmake --build desktop/build --config Release
```

`desktop/scripts/build.ps1` runs these steps. Package with
`python desktop/scripts/package.py --binary desktop/build/Release/lcb-hermes.exe`.
