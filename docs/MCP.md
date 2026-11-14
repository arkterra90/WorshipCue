# AI service orders through MCP

WorshipCue includes `WorshipCueMcp`, a local Model Context Protocol server. Connect it to an AI application that supports **local stdio MCP servers**, then ask that AI to assemble a service. The AI can find presentations and standalone media, inspect saved services, preview and save mixed service orders, edit shared song lyrics, and save named arrangements.

The AI model and conversation come from your chosen AI application. WorshipCue does not need an API key. Queried library text is returned to that application and is subject to its data settings. The MCP server has no network listener and makes no model requests. Planning Center tools make authenticated HTTPS reads to the Planning Center Services API.

## Connect an AI application

1. Build and reopen WorshipCue using the updated app.
2. Open **Settings → AI / MCP Setup…**.
3. Copy the connection configuration into your AI application's local MCP server settings. The dialog supplies the absolute executable path and the exact database opened by this WorshipCue window.
4. Enable the `worshipcue` server in your AI application. Restart or reconnect that client if its settings require it.

Use **Test Connection** in the setup dialog to check the actual companion's startup, MCP handshake, current tool discovery, and a read from this window's library. The test runs in the background with a ten-second timeout; closing the dialog stops it. It does not invoke editing tools, contact a model, or verify Planning Center credentials or your AI client's configuration. A successful test means the local server is usable; reconnect the AI client separately. Failure messages distinguish missing executables, startup/runtime trouble, outdated companions, protocol responses, library access, and timeouts. Reports contain no environment values or credentials.

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
| `list_media_assets` | Search standalone Media Library names/folders, with pagination. |
| `get_media_asset` | Inspect an active asset's type, name, folder, loop, and import date; unknown dates are null. |
| `update_song_section` | Preview or edit the shared plain lyrics of an existing section, preserving source identities. |
| `save_song_arrangement` | Preview, create, or replace a named arrangement of existing section references. |
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

The tool schemas supplied to the client describe exact parameters and limits. A creation item uses `song_id` for an existing presentation, `media_asset_id` alone for a standalone media asset, or `title` plus `slides` for a new text presentation. Presentation items support optional `slide_order` with **zero-based source-slide indices**; indices can repeat or omit slides. Omit it or use an empty array for natural order. Media items have one cue and do not accept `slide_order`. Repeated media entries share the asset and file. Recovery content cannot be reused until restored in WorshipCue.

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

The server exposes bounded library operations through the application's repository. It does not expose deletion, arbitrary SQL, file reads/import, licensing credentials, or taking slides live. Use WorshipCue to import media and manage Recovery. Library text and Planning Center content are data, never instructions to the AI.

## Edit shared sections and arrangements

Call `get_presentation` first. Its `revision` identifies the song's current title, source IDs, lyrics, rich formatting, section/arrangement metadata, and Look references. Pass that value as `expected_revision` to either editing tool. Missing, stale, invalid, or unknown inputs fail without saving. Read and review the song again after a successful save or stale-revision error before another edit. Transactions recheck the original state and roll back failed writes.

`update_song_section` takes `song_id`, `expected_revision`, `section_id`, and `slides`. Supply exactly the section's current `slide_count` plain-text pages (1–1000, each at most 65536 characters, at most 256 Ki characters per call). The tool preserves text exactly, including line breaks. Changing a page clears its old rich text override; unchanged pages keep their formatting. Every arrangement occurrence shares the updated lyrics. Source IDs, MIDI assignments, Looks, other sections, and custom service orders survive. For adding/removing pages or regrouping legacy songs, use **Song Sections / Arrangements…** in WorshipCue.

`save_song_arrangement` takes `song_id`, `expected_revision`, `name`, and ordered `section_ids`, including repeats. Omit `arrangement_id` to create an arrangement; provide an existing ID to update it. `make_active: true` selects it for natural-order service entries. Custom service orders keep their explicit source indices. Names must be unique within the song; missing sections, duplicate names, more than 100 arrangements, more than 10000 expanded slides per arrangement, or more than 50000 section references across the song are rejected.

Both editing tools default to `dry_run: true`. Review the returned lyrics or arrangement before calling with `dry_run: false`. A dry run's generated new-arrangement ID is only a preview; omit `arrangement_id` again when creating the saved arrangement. A save returns `saved: true` and the saved target ID. If its response is lost, inspect `get_presentation` before retrying. External edits refresh the operator's saved library automatically after modal editing finishes. The running live snapshot retains its captured lyrics, arrangement, cue position, and MIDI commands until the operator explicitly takes a slide again.

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

For a church-managed local connection, create a [Personal Access Token](https://api.planningcenteronline.com/personal_access_tokens) for an account with access to the desired Services plans and songs. Open **Settings → AI / MCP Setup…**, select **Include Planning Center credential placeholders**, and copy the configuration. Replace both placeholders in your MCP client's **server environment**, then restart its server connection:

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

WorshipCue does not store these credentials in SQLite or QSettings, accept them as tool arguments, or return them in tool results. Protect your AI client's configuration according to its credential-storage options. Song/sermon creation and all local tools work without Planning Center credentials. **Test Connection** checks the local companion/library; to verify Planning Center authentication, ask the AI to run `list_planning_center_service_types`.

Planning Center requires [OAuth for integrations spanning multiple churches](https://api.planningcenteronline.com/docs/overview/authentication). The companion also accepts `PLANNING_CENTER_ACCESS_TOKEN` as a Bearer token, taking precedence over the PAT pair. The OAuth provider must request the `services` scope and manage authorization/token renewal; this companion does not implement browser authorization or automatic token refresh. Reconnect the MCP client after replacing an expired token. A shared vendor PAT must not be distributed to churches.

### Prepare SongSelect lyrics in Planning Center

1. A Planning Center song administrator enables **Songs → Actions → Song File Integrations → SongSelect** with the church's eligible subscription.
2. Import **Lyrics** into the song's selected arrangement, then verify the text in **Lyrics & Chords**. An uploaded chord-chart PDF alone does not provide API lyric text. Editable ChordPro imports also populate that editor when included in the subscription.
3. Add that song and arrangement to the service plan and set its sequence. WorshipCue imports the selected arrangement's `lyrics` field, using the plan item's custom sequence when present and the arrangement sequence otherwise.
4. Ask the AI to preview the plan. Resolve every flagged item, review the lyric slides, then save the new service. Select it in WorshipCue and take it live when ready.

Planning Center's [SongSelect instructions](https://help.planningcenter.com/en/139448-set-up-the-songselect--by-ccli-integration.html) explain the required permissions, subscription features, and reconnection after inactivity. WorshipCue reads Planning Center; it does not sign in to SongSelect or download its PDF attachments.

### Read-only live readiness check

With credentials already configured in your terminal environment, run:

```sh
python3 tools/check-planning-center.py \
  --executable build-local/WorshipCue.app/Contents/MacOS/WorshipCueMcp \
  --plan-url https://services.planningcenteronline.com/plans/12345
```

Use your actual executable and plan URL. For direct lookup, replace `--plan-url` with `--service-type-id 10 --plan-id 20`. Omitting the plan checks authentication, Services access, and tool availability only. The checker uses a temporary library and `dry_run=true`; it never saves a service or changes Planning Center. It prints counts and actionable unresolved-item reasons without printing credentials or lyric text. Exit codes: `0` verified, `1` failed or unresolved, `2` missing credentials/invalid command. This tests the companion's environment; your AI client must receive the same credentials separately.

Ask the AI:

> Preview a WorshipCue service from this Planning Center plan: https://services.planningcenteronline.com/plans/12345. Match the songs, use the scheduled arrangements, and tell me what needs attention. Create a sermon presentation from the outline I provide, include it for the Message item, then save the service.

The AI can pass `plan_url` to `get_planning_center_plan` and `import_planning_center_service`. Alternatively, use a numeric-string `plan_id`, with optional `service_type_id`. When omitted, the server searches filtered plan collections among the first 100 accessible service types, with a bounded lookup time. Supply explicit IDs if that lookup cannot resolve the plan. Browse types/plans if you do not have a link. Listing tools accept `offset` and `limit` (default 50, maximum 100).

```json
{"plan_url":"https://services.planningcenteronline.com/plans/12345","dry_run":true}
```

Import defaults to **preview only** (`dry_run: true`). Review `source_items`, `warnings`, `unresolved_items`, and `ready_to_save`. To save, repeat with `dry_run: false`. A successful save creates a new service, including any new text presentations, in one transaction. Repeating a successful save creates another service; it is not an ongoing sync.

Import behavior:

- Pre-service, during-service, and post-service items keep that order; `sequence` orders each group. Headers are explicitly marked as skipped in the preview. You can override a header with a text slide.
- Unique exact title matches can reuse text presentations in the local library. Multiple matches require an explicit choice. Available arrangement lyrics can create missing songs. When a labelled sequence is provided, it must map to source slides; existing library slides are reused for that sequence only when they match the imported source lyrics. Otherwise the importer creates slides from the available lyrics or flags the item for resolution.
- New arrangement-lyric imports retain named sections and multi-page groups. Identical repeated headings/lyrics share canonical slides; the service's sequence references them in order. Custom labels from the selected sequence support Unicode text. Conflicting lyrics under one label and malformed sequence data require explicit resolution. Section metadata, slides, and the service commit together.
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

Run the `WorshipCue` target again after changing the companion to update the copy inside the macOS app. A distributable package needs Qt Core, Network (including its TLS backend), SQL, and the SQLite driver for the companion as well as the GUI's Qt dependencies. When deploying on macOS, include the bundled companion with `macdeployqt`'s `-executable` option. The [release packaging pipeline](RELEASING.md) now deploys and tests both executables together; signed/clean-machine release acceptance remains separate.

Without `--database`, the server uses the same organization/application names and default data location as WorshipCue. Explicit `--database` is recommended so the AI and operator always use the same library. Do not use `:memory:` for a persistent connection.

Check an installed macOS bundle without touching your library:

```bash
cmake --install build-local --prefix /private/tmp/worshipcue-mcp-install
/opt/homebrew/opt/qt/bin/macdeployqt /private/tmp/worshipcue-mcp-install/WorshipCue.app \
  -executable=/private/tmp/worshipcue-mcp-install/WorshipCue.app/Contents/MacOS/WorshipCueMcp
python3 tools/check-mcp-package.py /private/tmp/worshipcue-mcp-install/WorshipCue.app/Contents/MacOS/WorshipCueMcp
```

On Windows, deploy the GUI and companion with the matching Qt `windeployqt`, including Network/TLS and SQL/SQLite, then run `python tools/check-mcp-package.py <install-directory>/bin/WorshipCueMcp.exe`. The script checks deployed dependencies, removes developer Qt/environment settings, uses a temporary home/library/working directory, and verifies real stdio negotiation, discovery, media lookup, repeated mixed services, shared-section edits, arrangements, invalid-input recovery, and persistence across companion restarts. It is an MCP package check, not an installer, signing, or clean-OS certification.

If the setup dialog reports a missing server, rebuild the `WorshipCue` target. If the client reports missing Qt libraries or a SQLite driver, use the matching Qt environment or deploy its runtime dependencies. If a new service does not appear, verify both processes' database paths, close modal dialogs, and allow the next refresh. Reopening WorshipCue also reloads saved services.

Tests use temporary libraries. Run the protocol, persistence, and operator suites with:

```bash
ctest --test-dir build -R 'mcp_server|library_repository|operator_workflow' --output-on-failure
```

Validated on November 13, 2026 with macOS 15.7.4, Qt 6.12, and AppleClang 17: all 19 regression suites passed, with native access for Cocoa, CoreMIDI, and AVFoundation. The expanded MCP suite passes media discovery/mixed services, revision validation, shared lyrics/arrangements, rich-text/Look/MIDI identity preservation, SQL rollback, and real companion diagnostics. The setup dialog's successful connection test is visually inspected. The installed, ad-hoc-signed macOS bundle passed `check-mcp-package.py` with developer Qt/environment settings removed. This revealed and verified the fix for the console companion's bundle plugin discovery. Windows, clean OS images, real AI-client configuration, release signing/notarization, and live Planning Center accounts remain unverified. Planning Center tests use synthetic API responses.

The server supports the [2026-07-28 per-request protocol and discovery](https://modelcontextprotocol.io/specification/2026-07-28/basic/versioning), with legacy `initialize` handshakes for revisions 2025-11-25, 2025-06-18, 2025-03-26, and 2024-11-05. Modern discovery advertises the modern revision; legacy revisions negotiate through `initialize`. Transport uses [newline-delimited stdio](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/stdio), with an 8 MiB request-line limit and recovery after invalid or oversized lines. Automated tests cover discovery, both request styles, validation, dry runs, ordered persistence, malformed requests, subprocess framing/EOF, and failure recovery. Optional MCP features are only advertised when implemented.

## Media assets and structured songs

`get_service` returns `kind=media` and `media_asset_id` for a standalone media entry. Its `song_id` is null. Media entries have one cue. Presentation entries retain their existing IDs and fields.

Structured song creation through `create_song_presentation` preserves named lyric groups. Matching repeated headings and text reference one source section. The receipt reports unique `slide_count` and expanded `arranged_slide_count`. Different lyrics under the same heading get distinct names. `get_presentation` returns canonical source slides and `song_structure` metadata containing section IDs, slide counts, named arrangements, and the active arrangement ID.

`get_service` expands the saved arrangement when an entry has no custom slide order. `song_arrangement_id` identifies it. Explicit service slide orders use canonical source indices. `create_service_order` dry runs report the expanded count for an existing structured song. Standalone media registration and file import remain in the GUI. MCP can inspect existing assets and reuse them directly in mixed services, edit existing shared sections, and save arrangements. Existing presentation-only clients and tool arguments remain supported.
