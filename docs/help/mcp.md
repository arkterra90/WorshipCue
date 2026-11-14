# MCP configuration and usage

WorshipCue includes the **WorshipCueMcp** companion for AI clients that support local stdio MCP servers. The client provides the model and conversation. Queried library content is shared with that client under its data settings.

## Connect a client

1. Open **Settings → AI / MCP Setup…**.
2. Choose **Test Connection** to check companion startup, MCP negotiation, tool discovery, and access to this window's library.
3. Use **Copy Configuration** and add it to your client's local MCP server settings. Keep `command` as the executable path and each `args` entry as a separate argument.
4. Enable the server and reconnect or restart the AI client as required.

On macOS, the companion is inside `WorshipCue.app/Contents/MacOS`. On Windows, it sits beside the GUI executable. Update client paths when the app or library moves. A remote-URL-only connector requires a client that can launch local stdio servers.

## Work with the library

Ask the AI to search presentations, inspect source slides and saved services, and find standalone media. Ask it to preview a proposed service before saving. A service save creates a new service, including any new text presentations in the same transaction. Repeated media entries share files.

For example, ask it to create a service called Sunday Morning with a welcome slide, two existing library songs, a message title slide, and a closing song, then show the proposed order before saving.

Section and arrangement edits require the current revision from `get_presentation` and default to `dry_run=true`. Shared lyric edits update every saved section occurrence. Read and review the song again after saving. Live output captures external edits on the operator's next explicit take.

WorshipCue refreshes saved content after external commits, deferring refresh during modal editing and dragging. Choose the new service in the selector to review it.

## Planning Center

1. In Planning Center, connect the church's SongSelect account under **Songs → Actions → Song File Integrations**. Import **Lyrics** into the song's chosen arrangement and check **Lyrics & Chords**. A PDF attachment alone is insufficient.
2. Add that song and arrangement to a Planning Center plan and check its sequence.
3. In WorshipCue, open **Settings → AI / MCP Setup…**, select **Include Planning Center credential placeholders**, and copy the configuration. In your AI client's server settings, replace both placeholders with your church's Personal Access Token application ID and secret, then reconnect the client. Keep credentials private.
4. Ask the AI to list Planning Center service types to verify account access. The app's **Test Connection** checks only the local companion and library.
5. Give the AI your plan link and ask for a preview. Resolve missing lyrics, ambiguous library matches, or unsupported items before saving. Newly imported lyrics retain named sections and the service's repeated song sequence.
6. Select the saved service in WorshipCue, review the slides, and take it live when ready.

Planning Center access requires internet connectivity and permission to read the selected plans and songs. Imports create a new local service and leave Planning Center unchanged. OAuth-managed connections can supply `PLANNING_CENTER_ACCESS_TOKEN`; their provider manages authorization and renewal. A distributed multi-church integration requires OAuth.

[Troubleshooting](troubleshooting.md) · [Songs and arrangements](songs-arrangements.md) · [Service playlists](service-playlists.md)
