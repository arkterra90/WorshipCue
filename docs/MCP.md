# AI service orders through MCP

WorshipCue includes `WorshipCueMcp`, a local Model Context Protocol server. Connect it to an AI application that supports **local stdio MCP servers**, then ask that AI to assemble a service. The AI can find library content, inspect slides and saved services, preview a proposed order, and save a new service with existing presentations and new text slides.

The AI model and conversation come from your chosen AI application. WorshipCue does not need an API key. Queried library text is returned to that application and is subject to its data settings. The MCP server has no network listener and makes no model requests. Planning Center tools make authenticated HTTPS reads to the Planning Center Services API.

## Connect an AI application

1. Build and reopen WorshipCue using the updated app.
2. Open **Settings → AI / MCP Setup…**.
3. Copy the connection configuration into your AI application's local MCP server settings. The dialog supplies the absolute executable path and the exact database opened by this WorshipCue window.
4. Enable the `worshipcue` server in your AI application. Restart or reconnect that client if its settings require it.

The configuration has this shape; use the dialog's real paths:

```json
{
  "mcpServers": {
    "worshipcue": {
      "command": "/absolute/path/to/WorshipCueMcp",
      "args": ["--database", "/absolute/path/to/library.sqlite3"]
    }
  }
}
```

Some clients store `command` and `args` in a different configuration format. Copy those two values into that client's server entry. `command` is an executable path, not a shell command; keep each argument separate. A remote-URL-only connector cannot launch this local server.

On macOS the companion is inside `WorshipCue.app/Contents/MacOS/WorshipCueMcp`. On Windows it is `WorshipCueMcp.exe` beside `WorshipCue.exe`. Keep the companion with the app when moving it and update the client configuration after moving the app or library.

The client launches and stops the server. WorshipCue can be open or closed. While open, it checks for external database commits approximately once per second and refreshes the library and saved service selector, deferring refresh during modal editing and dragging. Select the newly named service to review it. The current service selection and live output remain under operator control.

## Ask the AI to plan a service

For example:

> Create a service called “Sunday Morning.” Start with a welcome slide, use two suitable songs from my WorshipCue library, add a message title slide called “A Life of Gratitude,” and finish with a closing song from the library. Show me the proposed order first, then save it when I ask.

Or:

> Inspect last week's service and create a new service called “Sunday Evening” using its songs in reverse order. Keep the saved arrangements. Include a new opening slide that says “Welcome to Sunday Evening.”

The AI should search for actual presentation IDs before referencing existing content. Titles are not unique. Songs can appear more than once in a service, with an independent slide order for each occurrence. For new lyrics, provide the text you want to use or ask the AI for original text.

## Available tools

| Tool | Purpose |
|---|---|
| `search_library` | Find presentations by title, lyrics, or media names and retrieve their IDs. |
| `get_presentation` | Inspect a presentation and its source slides before choosing or arranging it. |
| `list_services` | Find saved service names and IDs. |
| `get_service` | Read a saved service's ordered entries and their arranged slides. |
| `create_service_order` | Preview or atomically save a new named service using existing content and/or new text presentations. |
| `create_song_presentation` | Create an editable library song from lyrics or explicit slides. |
| `create_sermon_presentation` | Create an editable sermon with a title/speaker/Scripture slide and content slides. |
| `list_planning_center_service_types` | Find service type IDs in your Planning Center account. |
| `list_planning_center_plans` | Browse that service type's plans, newest dates first. |
| `get_planning_center_plan` | Read a plan URL or ID, its ordered items, and available arrangement lyrics. |
| `import_planning_center_service` | Preview or create a WorshipCue service from a Planning Center plan. |

Search, service listing, and presentation inspection accept `offset`/`limit` and return `next_offset` when more results remain. `get_service` also pages entries (default 50, maximum 100). It previews up to 20 arranged slides per entry; use `get_presentation` to inspect additional source slides. `natural_order: true` with an empty `slide_order` means the source's natural order. Large results mark omitted text or arrangements with `text_truncated`, `slides_truncated`, or `slide_order_truncated` so the AI can request the required source content instead of treating a preview as complete.

The tool schemas supplied to the client describe exact parameters and limits. A creation item uses either `song_id` for an existing presentation or `title` plus `slides` for a new text presentation. Optional `slide_order` contains **zero-based source-slide indices**; indices can repeat or omit slides. Omit it or use an empty array for natural order.

Example `create_service_order` arguments (replace `12` with an ID returned by `search_library`):

```json
{
  "name": "Sunday Morning",
  "dry_run": true,
  "items": [
    {"title": "Welcome", "slides": ["Welcome\nWe are glad you are here."]},
    {"song_id": 12, "slide_order": [0, 1, 0]},
    {"title": "Message", "slides": ["A Life of Gratitude"]},
    {"song_id": 12}
  ]
}
```

`dry_run: true` validates the proposed order and returns a preview without saving it. To save, call the tool with `dry_run: false` or omit `dry_run`. The saved result includes the service ID. Every successful save creates a **new** service; retrying a successful save creates another service, even if the name matches. Check `list_services` if the client loses the response before retrying.

New text presentations are saved in the shared library and can be edited and reused in WorshipCue. Existing presentations keep their text, media, backgrounds, timing, and transitions. Invalid IDs, out-of-range arrangements, invalid text, or a failed database write cancel the entire save, including newly created library items. No partial service is left behind.

The server exposes planning and new-service creation. It does not expose deletion, editing of existing library text, file import, or taking slides live.

## Song and sermon presentations

`create_song_presentation` takes a `title` and either `lyrics` or `slides`. Lyrics use the same importer as WorshipCue: section headings, blank lines, and `---` separate slides, and bracketed chord annotations are removed. Explicit slides give the AI exact control of the layout's text boundaries.

```json
{"title":"An Original Song","lyrics":"[Verse 1]\nWe gather here today\n\n[Chorus]\nTogether we give thanks","dry_run":true}
```

`create_sermon_presentation` takes a `title`, optional `speaker` and `scripture` reference, and a nonempty array of content `slides`. It adds a title slide before those content slides:

```json
{"title":"A Life of Gratitude","speaker":"Jordan","scripture":"Psalm 100","slides":["Give thanks in daily life","Notice the people who help you","Practice gratitude this week"],"dry_run":true}
```

Both support `dry_run` (default false). A saved presentation returns its actual `song_id`, which can be referenced in `create_service_order`. They create reusable library content without creating a service. Sermons use WorshipCue's existing editable text presentation format and renderer; this does not introduce a separate sermon editor or database content category. Slide formatting is editable in the app. Preview responses show up to 20 slides and mark truncation. Supply the actual Scripture text if you want verse slides; a Scripture reference alone appears on the title slide.

## Connect Planning Center Services

Create a [Personal Access Token](https://api.planningcenteronline.com/personal_access_tokens) for an account with access to the desired Services plans. Planning Center documents [Personal Access Token authentication](https://github.com/planningcenter/developers#authentication) using an application ID and secret. Add these to your MCP client's **server environment**, then restart the server connection:

```json
{
  "mcpServers": {
    "worshipcue": {
      "command": "/absolute/path/to/WorshipCueMcp",
      "args": ["--database", "/absolute/path/to/library.sqlite3"],
      "env": {
        "PLANNING_CENTER_APPLICATION_ID": "YOUR_PERSONAL_ACCESS_TOKEN_APPLICATION_ID",
        "PLANNING_CENTER_SECRET": "YOUR_PERSONAL_ACCESS_TOKEN_SECRET"
      }
    }
  }
}
```

The app's setup dialog links to token creation and names the environment settings. WorshipCue does not store these credentials in SQLite or QSettings, accept them as tool arguments, or return them in tool results. Protect your AI client's configuration according to its credential-storage options. Song/sermon creation and all local tools work without Planning Center credentials.

Ask the AI:

> Preview a WorshipCue service from this Planning Center plan: https://services.planningcenteronline.com/plans/12345. Match the songs, use the scheduled arrangements, and tell me what needs attention. Create a sermon presentation from the outline I provide, include it for the Message item, then save the service.

The AI can pass `plan_url` to `get_planning_center_plan` and `import_planning_center_service`. Alternatively, use a numeric-string `plan_id`, with optional `service_type_id`; the server resolves the service type when omitted. Browse types/plans if you do not have a link. Listing tools accept `offset` and `limit` (default 50, maximum 100).

```json
{"plan_url":"https://services.planningcenteronline.com/plans/12345","dry_run":true}
```

Import defaults to **preview only** (`dry_run: true`). Review `source_items`, `warnings`, `unresolved_items`, and `ready_to_save`. To save, repeat with `dry_run: false`. A successful save creates a new service, including any new text presentations, in one transaction. Repeating a successful save creates another service; it is not an ongoing sync.

Import behavior:

- Pre-service, during-service, and post-service items keep that order; `sequence` orders each group. Headers are explicitly marked as skipped in the preview. You can override a header with a text slide.
- Unique exact title matches can reuse text presentations in the local library. Multiple matches require an explicit choice. Available arrangement lyrics can create missing songs. When a labelled sequence is provided, it must map to source slides; existing library slides are reused for that sequence only when they match the imported source lyrics. Otherwise the importer creates slides from the available lyrics or flags the item for resolution.
- Regular plan items become title slides. Planning descriptions and `html_details` are returned as source data when inspecting the plan, but are **not projected automatically**. Supply audience-ready sermon/announcement slides using an override.
- Media files and attachments are not downloaded. Media items, missing lyrics, ambiguous matches, and unmappable sequences require overrides before saving. `allow_placeholders: true` explicitly permits title-only placeholders and records warnings. Nothing is silently filled with invented lyrics or sermon text.
- Repeated existing library songs remain separate playlist occurrences. New text definitions are saved per occurrence, consistent with `create_service_order`.

Override keys are Planning Center **item IDs** from the preview. Each override is either an existing `song_id` (optional `slide_order`), a new `title`/`slides` definition (optional `slide_order`), or exactly `{"skip":true}`. An empty override is invalid. For example:

```json
{
  "plan_url":"https://services.planningcenteronline.com/plans/12345",
  "dry_run":false,
  "item_overrides":{
    "42":{"song_id":17,"slide_order":[0,1,0]},
    "43":{"title":"A Life of Gratitude","slides":["A Life of Gratitude\nJordan\nPsalm 100","Give thanks in daily life"]},
    "44":{"skip":true}
  }
}
```

Planning Center access is read-only. Requests use the documented [Services API schema](https://api.planningcenteronline.com/services/v2/open_api/2018-11-01), authenticated HTTPS to its fixed API host, no redirects, bounded responses/pagination, and timeouts. Authentication/permission errors, rate limiting, invalid pagination, and incomplete downloads return errors before local content is saved. The integration requires your own credentials for live use; automated integration tests use representative JSON:API fixtures rather than a real church account.

## Build, run, and troubleshoot

The normal `WorshipCue` build also builds the companion and places it with the app. Building the command-line server alone is useful during development:

```bash
cmake --build build-local --target WorshipCueMcp --parallel 4
./build-local/WorshipCueMcp --help
```

Run the `WorshipCue` target again after changing the companion to update the copy inside the macOS app. A distributable package needs Qt Core, Network (including its TLS backend), SQL, and the SQLite driver for the companion as well as the GUI's Qt dependencies. When deploying on macOS, include the bundled companion with `macdeployqt`'s `-executable` option. Distribution signing and installers remain separate work.

Without `--database`, the server uses the same organization/application names and default data location as WorshipCue. Explicit `--database` is recommended so the AI and operator always use the same library. Do not use `:memory:` for a persistent connection.

If the setup dialog reports a missing server, rebuild the `WorshipCue` target. If the client reports missing Qt libraries or a SQLite driver, use the matching Qt environment or deploy its runtime dependencies. If a new service does not appear, verify both processes' database paths, close modal dialogs, and allow the next refresh. Reopening WorshipCue also reloads saved services.

Tests use temporary libraries. Run the protocol, persistence, and operator suites with:

```bash
ctest --test-dir build -R 'mcp_server|library_repository|operator_workflow' --output-on-failure
```

Validated on November 12, 2026 with macOS 15.7.4, Qt 6.12, and AppleClang 17: MCP and repository suites passed, the bundled executable passed a real stdio save test, and all three new operator checks passed (external refresh/live preservation, setup paths, and failed-refresh recovery). The other regression suites passed, including native video playback. Two existing keyboard tests could not activate their native macOS windows in the test session; both passed with the offscreen backend. Windows was not tested. Song/sermon creation and Planning Center import tests also pass, covering direct plan links, pagination, pre/during/post ordering, section sequences, missing songs/media, explicit overrides, invalid overrides, and foreign pagination rejection. Planning Center tests use synthetic API responses; no live church account was connected.

The server supports the [2026-07-28 per-request protocol and discovery](https://modelcontextprotocol.io/specification/2026-07-28/basic/versioning), with legacy `initialize` handshakes for revisions 2025-11-25, 2025-06-18, 2025-03-26, and 2024-11-05. Modern discovery advertises the modern revision; legacy revisions negotiate through `initialize`. Transport uses [newline-delimited stdio](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/stdio), with an 8 MiB request-line limit and recovery after invalid or oversized lines. Automated tests cover discovery, both request styles, validation, dry runs, ordered persistence, malformed requests, subprocess framing/EOF, and failure recovery. Optional MCP features are only advertised when implemented.
