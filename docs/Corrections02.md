- For example they set a certain font and font size for a slide. They should be able to apply that to all the slides in a presentation.

# WorshipCue — Corrections 2
## Product Improvements, Licensing, Output Configuration, and Distribution

You are continuing development of **WorshipCue**, an existing native desktop presentation application built with C++ and Qt 6 for macOS and Windows.

The application already compiles and runs. Your task is to implement the corrections and enhancements below while maintaining existing functionality, architectural consistency, and a reliable user experience.

## Development Instructions

Before making changes:

1. Inspect the existing repository, architecture, database schema, and implemented features.
2. Identify which requested features already exist and which require modification or new development.
3. Reuse existing components and systems wherever practical.
4. Preserve compatibility with existing presentation files, libraries, and service playlists.
5. Make appropriate database migrations rather than introducing breaking schema changes.
6. Prioritize reliability, usability, maintainability, and performance.
7. Follow native Qt 6 conventions for macOS and Windows.
8. Implement features in the priority order below, completing and testing each logical group before proceeding.
9. Do not replace working functionality with placeholders or simulations.
10. Update user documentation and automated tests as features are completed.

Do not rewrite the application from scratch.

If a feature requires external accounts, credentials, hardware, or services that are unavailable, implement and test everything possible locally, document the remaining setup, and clearly identify what cannot yet be verified.

---

# PRIORITY 1 — Content Organization and Song Management

## 1.1 Separate Media Files From Presentations

WorshipCue must distinguish between:

- **Media Items:** Individual images, videos, audio files, or other supported media assets.
- **Presentations:** Structured slide-based content such as songs, sermons, and announcements.
- **Service Playlists:** Ordered collections of presentations and media items used during a service.

### Requirements

Add a third primary library section:

1. Content Library
2. Service Playlist
3. Media Library

The Media Library should:

- Store and organize individual media assets.
- Support importing, previewing, renaming, organizing, and deleting media.
- Display relevant file metadata.
- Support dragging media into service playlists.
- Allow media to be triggered directly during live presentations.
- Avoid converting individual media items into slide presentations unnecessarily.
- Maintain references to shared media assets rather than duplicating files.

Use a clear data model that differentiates media assets, presentations, and playlist entries.

## 1.2 Structured Song Lyrics and Arrangements

Song lyrics must be organized into reusable, editable sections.

### Supported Sections

Examples include:

- Verse 1, Verse 2, Verse 3
- Chorus
- Pre-Chorus
- Bridge
- Intro
- Outro
- Tag
- Refrain
- Instrumental
- Custom sections

### Song Editing

Users must be able to:

- Create and edit individual song sections.
- Reorder sections.
- Duplicate references to sections in an arrangement.
- Create a complete arrangement using those sections.
- Save and reuse arrangements.
- Navigate sequentially through the arranged slides during a presentation.

Example:

Verse 1 → Chorus → Verse 2 → Chorus → Bridge → Chorus → Chorus

### Shared Section Editing

Each unique section must have a single authoritative source.

If the user edits the Chorus, every occurrence of that Chorus within the song arrangement must update automatically.

The same behavior must apply to verses, bridges, tags, and other sections.

Repeated sections should reference the original section rather than contain independently duplicated lyrics.

Ensure edits update slide previews and presentation rendering immediately.

Where a section requires multiple slides, preserve its logical grouping while allowing individual slide navigation.

---

# PRIORITY 2 — Slide Editing, Formatting, and Navigation

## 2.1 Presentation Looks and Scenes

Users must be able to define and reuse presentation-wide visual formatting.

Provide a system for applying a **Look**, **Scene**, or equivalent style configuration to presentations.

A Look should support:

- Font family
- Font size
- Font weight and style
- Text color
- Alignment
- Text positioning
- Margins and padding
- Background color or media
- Other existing visual properties supported by WorshipCue

### Application

Users should be able to:

1. Format an individual slide.
2. Save those formatting choices as a reusable Look.
3. Apply the Look to every slide within a presentation.
4. Apply a Look to selected slides.
5. Change the Look without modifying the underlying lyric or sermon content.

Use shared styling references where appropriate, with optional slide-specific overrides.

### Text Overflow Handling

When a Look causes text to exceed slide boundaries:

- Detect overflow using actual rendering measurements.
- Warn the user before applying destructive changes.
- Offer automatic text splitting.
- Preserve the original text and reading order.
- Maintain song section associations.
- Support previewing the resulting split slides.
- Allow the operation to be undone.

Auto-splitting should respect line breaks and natural phrasing whenever possible.

Do not silently clip content.

## 2.2 Keyboard Navigation

Fix keyboard navigation during presentations.

Required behavior:

| Input | Action |
|---|---|
| Spacebar | Advance one slide |
| Right Arrow | Advance one slide |
| Down Arrow | Advance one slide |
| Left Arrow | Previous slide |
| Up Arrow | Previous slide |

Requirements:

- Navigation must work consistently across service playlist presentations.
- Moving backward must restore the correct previous slide.
- Keyboard commands must not interfere with text entry or editing.
- Navigation must work with song arrangements and media items.
- Existing mouse-based navigation must continue working.

Use a centralized navigation controller rather than duplicating navigation logic.

---

# PRIORITY 3 — Transitions, Fades, and Playback Controls

## 3.1 Global and Presentation-Level Transitions

Implement a predictable hierarchy for fade and transition settings.

Priority:

1. Explicit presentation-level overrides
2. Global application defaults

Blackout and Clear Text operations must follow this hierarchy.

Global transition settings should determine their behavior unless the active presentation specifies an override.

Ensure that blackout and clear operations do not unintentionally change the active slide or service playlist position.

## 3.2 Bulk Transition Editing

Users must be able to:

- Select multiple service playlist items.
- Select all service playlist items.
- Apply identical fade settings.
- Apply identical transition settings.
- Adjust transition duration and type.
- Override those settings on individual presentations afterward.

Use the existing selection behavior and context menus where practical.

Bulk changes must be undoable and should not overwrite unrelated presentation settings.

---

# PRIORITY 4 — Multiple Video Outputs

WorshipCue must support configurable, independent presentation outputs.

## 4.1 Output Architecture

Support up to four independently configurable logical presentation outputs:

- Audience Screen 1
- Audience Screen 2
- Stage Screen 1
- Stage Screen 2

Each output must support independent configuration and rendering.

Users should be able to assign each logical output to an available physical display.

Avoid creating unnecessary duplicate rendering pipelines when common rendering resources can be shared safely.

## 4.2 Output Settings

WorshipCue should detect connected displays and their supported native display modes where exposed by the operating system.

Provide configurable options for:

- Display assignment
- Resolution
- Refresh rate where supported
- Progressive or interlaced output where supported
- Aspect ratio
- Output orientation
- Custom logical rendering resolution
- Scaling and letterboxing behavior

Users should be able to manually enter custom output dimensions and aspect ratios.

Distinguish between a custom rendering canvas and changing the physical display signal. Use actual platform capabilities rather than assuming Qt can set every video hardware mode.

Validate settings against hardware and operating-system capabilities. Clearly explain unsupported combinations.

Persist configuration across application restarts.

Handle disconnected, reconnected, and rearranged displays gracefully.

## 4.3 Customizable Stage Displays

Preserve the existing standard stage display as the default.

Add a configurable stage display editor supporting elements such as:

- Current slide
- Next slide
- Current song or presentation title
- Clock
- Countdown timers
- Stage messages
- Custom text
- Background colors
- Font and layout settings

Users should be able to create, save, edit, and assign stage display layouts independently to Stage Screen 1 and Stage Screen 2.

Audience screens should also support independent presentation output configurations.

Test rendering with different resolutions and aspect ratios.

---

# PRIORITY 5 — MIDI Integration

MIDI is a required component of WorshipCue's professional control system.

Implement configurable MIDI input and output on macOS and Windows.

Use established MIDI libraries or platform APIs appropriate for Qt 6.

## 5.1 MIDI Device Configuration

Create a MIDI configuration interface where users can:

- Discover available MIDI devices.
- Select MIDI input sources.
- Select MIDI output destinations.
- Configure MIDI channels.
- Configure messages and parameters.
- Enable or disable devices.
- Save configurations.
- Automatically reconnect to previously configured devices.
- Monitor incoming and outgoing MIDI activity.

Support standard MIDI note messages and control-change messages where appropriate.

Support virtual MIDI ports and network MIDI through compatible platform facilities where available.

## 5.2 Slide-Level MIDI Actions

Users must be able to right-click a slide and assign MIDI behavior.

### MIDI Output

A slide can trigger one or more MIDI output commands when activated.

Examples:

- MIDI Note On
- MIDI Note Off
- Control Change

Allow configuration of the destination, channel, message, note/controller number, and velocity/value as applicable.

### MIDI Input

Users can assign a MIDI input trigger to activate a specific slide.

When WorshipCue receives the matching signal, it should activate that slide according to a clearly defined mapping.

Provide conflict detection and predictable behavior when multiple slides or commands use the same MIDI trigger.

Avoid accidental retriggering or feedback loops.

## 5.3 Global MIDI Commands

Allow users to configure global MIDI input assignments for:

- Next slide
- Previous slide
- Blackout
- Clear Text
- Restore output
- Select or trigger a presentation
- Other existing global playback controls

These should be configurable in a centralized MIDI mapping interface.

## 5.4 ProPresenter Reference

Use ProPresenter's MIDI workflow as a usability and behavior reference, without copying its proprietary implementation or interface assets.

Review these official documentation sources:

ProPresenter MIDI Configuration:
https://support.renewedvision.com/hc/en-us/articles/1500000020301-Devices-MIDI-ProPresenter-Setup

System MIDI Configuration:
https://support.renewedvision.com/hc/en-us/articles/4412376777491-Devices-MIDI-System-Setup

Pay particular attention to:

- Device configuration
- MIDI mapping
- Input and output assignments
- Slide-level MIDI actions
- Global controls
- MIDI feedback and troubleshooting

Implement equivalent useful functionality using WorshipCue's existing architecture and visual conventions.

---

# PRIORITY 6 — Library Cleanup and Storage Management

Add a storage management feature to help users remove unused content.

## Requirements

Create a library cleanup interface accessible through application settings or library management.

Users must be able to:

1. Enter a custom number of days.
2. Find content unused for longer than that period.
3. Review matching content before deletion.
4. Select individual items or all eligible items.
5. See estimated recoverable storage.
6. Confirm deletion.

Track actual usage timestamps where practical.

Distinguish between modification dates, import dates, and last-used dates.

### Deletion Safety

- Never automatically delete content without confirmation.
- Protect content referenced by active service playlists or presentations.
- Account for shared files referenced by multiple presentations.
- Explain why protected items cannot be deleted.
- Prefer reversible deletion or a recovery mechanism where feasible.
- Do not use a missing usage timestamp as proof an item is unused.
- Provide a preview before permanent removal.

Ensure existing libraries survive database migrations.

---

# PRIORITY 7 — MCP Integration

MCP should be a first-class feature of WorshipCue.

Review the existing MCP implementation and improve its integration, reliability, and documentation.

## Requirements

- Maintain compatibility with the Model Context Protocol.
- Make MCP functionality easy to discover in application settings or Help.
- Explain how users connect WorshipCue to compatible AI clients.
- Generate or display appropriate client configuration examples.
- Support the existing development and production execution paths.
- Provide useful diagnostic information when MCP fails to connect.
- Preserve compatibility with the existing application database and data model.

MCP should expose useful, well-structured operations for working with libraries, songs, presentations, and service playlists.

Any operation that modifies data must validate inputs and respect application integrity.

Do not expose licensing secrets or unrestricted database operations through MCP.

Ensure MCP functions appropriately in the packaged production application.

---

# PRIORITY 8 — Local Documentation and Help Center

WorshipCue needs a built-in documentation hub that works without an internet connection.

## Requirements

Create an accessible Help or Documentation section within the application.

Include:

1. Getting started
2. Creating presentations
3. Managing the Content Library
4. Managing the Media Library
5. Building service playlists
6. Editing songs and arrangements
7. Slide formatting and Looks
8. Presentation navigation
9. Audience and stage display setup
10. Transitions and fades
11. MIDI configuration
12. MCP configuration and usage
13. License activation and device management
14. Storage management
15. Troubleshooting

Documentation should:

- Be included in packaged application resources.
- Work offline.
- Be searchable if practical.
- Be organized with clear navigation.
- Include keyboard shortcuts.
- Contain accurate instructions matching the implemented UI.

Provide a visible Help menu entry.

Avoid documentation that references unfinished functionality as though it is available.

---

# PRIORITY 9 — Licensing and Authorization

Create a secure licensing system that distinguishes between editing and live presentation capabilities.

## 9.1 Application Modes

### Unlicensed Mode

Without a valid license, WorshipCue must continue functioning as a useful presentation editor.

Allow:

- Content Library management
- Media Library management
- Song and sermon editing
- Service playlist creation
- Saving and editing presentations
- Other non-live editing functions

Disable actual live presentation output and any equivalent mechanism that bypasses the licensing restriction.

### Licensed Mode

A valid license enables live presentation functionality.

The licensing experience should be simple and avoid unnecessarily interfering with normal usage.

## 9.2 Cloudflare Licensing Backend

Build the license verification system using:

- Cloudflare Workers for the licensing API
- Cloudflare D1 for persistent licensing records
- Appropriate Cloudflare security and deployment practices

The Cloudflare service must be authoritative for license issuance and activation.

### License Data

Track:

- License identifier
- Secure license code verifier or equivalent credential
- License status
- Maximum activations
- Current activated installations
- Installation identifiers
- Activation and verification timestamps
- Revocation information

Never store sensitive license credentials unnecessarily in plaintext.

## 9.3 Five-Machine Activation Limit

Each license must authorize up to **five distinct machine installations**.

Required behavior:

- First activation registers the machine.
- Repeated verification on the same installation does not consume another activation.
- An attempt to activate a sixth machine is rejected.
- Users receive a clear explanation when the limit is reached.
- Provide a secure way to deactivate and transfer a license to another machine.

Use a privacy-conscious machine installation identifier with appropriate protections against trivial duplication.

Document reinstall and hardware-change behavior.

## 9.4 License Validation

At application startup:

1. Check the locally stored license state.
2. Validate its authenticity and expiration.
3. Verify against Cloudflare when required.
4. If the local authorization is invalid or expired, attempt online verification.
5. If no valid authorization is available, enter editing-only mode and provide an activation prompt.

Use server-signed, short-lived local authorization leases where appropriate.

A locally cached authorization must be verifiable without storing a private signing key in the application.

Define a reasonable offline authorization period so temporary internet outages do not unnecessarily disable licensed live presentations.

Enforce activation limits and revocation decisions on the server.

## 9.5 Security

Use defense-in-depth licensing practices.

Requirements:

- HTTPS for licensing communication
- Server-side license issuance and activation enforcement
- Signed local authorization data
- Protected local credential storage using operating-system facilities where practical
- No embedded Cloudflare API secrets or private signing keys
- Rate limiting and abuse protection
- Replay-resistant activation and deactivation operations
- Secure handling of license revocation
- No client-controlled activation counts
- Safe error handling and logging
- Tests for invalid, expired, revoked, and duplicated activations

Client-side software cannot be made completely tamper-proof. Design the system to resist practical bypasses and keep authoritative state on Cloudflare rather than claiming absolute protection.

Do not rely solely on an easily modified local Boolean or configuration file.

## 9.6 Development License

Create one valid developer license for local testing.

Important:

- The license must be created using the real Cloudflare licensing backend.
- Do not invent a license that the backend cannot validate.
- Do not commit the license code or credentials to Git.
- Store development secrets in ignored local files or secure credential storage.
- Update `.gitignore` appropriately.
- Provide a safe local development setup.

If Cloudflare credentials or deployment access are missing, prepare the infrastructure and explain exactly what must be configured before issuing the real developer license.

Never print private credentials into committed source files or logs.

---

# PRIORITY 10 — Application Packaging and Distribution

Prepare WorshipCue for distribution on macOS and Windows.

## Requirements

Create repeatable release builds using the existing CMake/Qt build system.

### macOS

Produce a distributable application with an appropriate installer or disk image.

Account for:

- Bundled Qt dependencies
- Application icons and metadata
- Required runtime components
- Code signing
- Apple notarization when credentials are available

### Windows

Produce a standard installer executable with:

- Bundled runtime dependencies
- Application shortcuts
- Installation and uninstall support
- Appropriate application metadata
- Code signing when credentials are available

## User Experience

The goal is for users to:

1. Visit the WorshipCue website.
2. Download the installer for their operating system.
3. Open the downloaded installer.
4. Complete installation using a straightforward native workflow.
5. Launch WorshipCue without manually installing Qt or other development dependencies.

The packaged application must include:

- All required resources
- Built-in documentation
- MCP components
- Necessary database migrations
- License activation functionality

Create reproducible packaging scripts and document the release procedure.

Clearly distinguish fully signed/notarized releases from unsigned development builds.

Do not claim distribution readiness until the installers have been built and tested.

---

# IMPLEMENTATION AND VALIDATION

## Architecture

Favor reusable components and clear separation of concerns.

Suggested subsystems, where compatible with the existing architecture:

- Library and asset management
- Song structure and arrangement management
- Presentation renderer
- Navigation controller
- Looks and formatting manager
- Output and screen manager
- Transition manager
- MIDI manager
- Licensing client
- Cloudflare licensing service
- MCP service
- Documentation service

These are logical responsibilities, not a requirement to create a separate class for every item.

Avoid unnecessary architectural complexity.

## Testing Requirements

Add or update tests for:

- Shared lyric section edits
- Song arrangement navigation
- Media/presentation data separation
- Keyboard navigation
- Style application
- Text overflow and splitting
- Transition inheritance and bulk updates
- Multiple output configurations
- MIDI input/output mappings
- Storage cleanup safety
- MCP operations
- License validation and five-machine limits
- Application packaging and resource inclusion

Use automated testing where practical and identify tests that require physical displays, MIDI devices, OS signing credentials, or Cloudflare access.

Verify existing behavior remains functional.

## Execution Strategy

Work through the priorities incrementally.

For each major feature:

1. Inspect relevant existing code.
2. Determine the smallest coherent implementation.
3. Make the required changes.
4. Build and run relevant tests.
5. Fix regressions.
6. Update documentation.
7. Continue to the next feature.

Do not perform a broad speculative rewrite.

If implementation of all requested functionality is too large for one session, complete the highest-priority working features first and leave a precise implementation plan for the remaining items.

## Completion Report

When finished, provide:

1. **Implemented:** Features completed and tested.
2. **Modified Files:** Important files and their purposes.
3. **Database Changes:** Any migrations or schema changes.
4. **Build Status:** Results for available build targets.
5. **Tests:** Tests run and their outcomes.
6. **External Setup:** Cloudflare, signing, or hardware requirements.
7. **Remaining Work:** Anything incomplete or unverified.
8. **Next Steps:** The next highest-priority tasks.

Do not report partially implemented features as complete.

## Final Objective

WorshipCue should become a reliable, professional, volunteer-friendly native worship presentation application.

The experience should feel familiar to users of ProPresenter while preserving WorshipCue's own identity and architecture.

Prioritize intuitive operation during live worship services, strong content management, dependable multimedia output, practical automation, secure licensing, and straightforward installation.

**Begin by inspecting the current repository and identifying the existing implementation of each priority. Then begin implementing the corrections in order.**
