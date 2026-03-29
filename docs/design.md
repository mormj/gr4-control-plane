# Design

## Product Goal

Build a minimal C++23 REST control plane that can create, inspect, and drive GNU Radio 4 sessions, expose a narrow live block-settings surface, and serve a read-only GNU Radio 4 block catalog. The product goal is a small session runner appliance, not a general-purpose control platform.

## Minimal Philosophy

- Prefer simplicity over extensibility.
- Keep one process and minimal dependencies.
- Push business logic into application services rather than route handlers.
- Treat new scope as suspect unless it directly serves session lifecycle control.
- Preserve clean boundaries so future work can be added without contaminating the MVP.

## Architecture Layers

- `domain`: pure types such as `Session` and `SessionState`
- `storage`: repository boundary, in-memory only for the MVP
- `runtime`: runtime execution boundary, stubbed for now but invoked by the service
- `app`: `SessionService` for session lifecycle plus `BlockSettingsService` for live block settings control
- `api`: thin HTTP transport layer that delegates to `SessionService`, `BlockSettingsService`, and `BlockCatalogService`
- `catalog`: provider-backed read-only block metadata for future studio integration

## Thin HTTP Layer

The HTTP layer is intentionally small:

- parse request JSON and path parameters
- call `SessionService`
- call `BlockCatalogService`
- call `BlockSettingsService`
- serialize session and error responses
- map application exceptions to HTTP status codes

It must not contain lifecycle rules, runtime orchestration, catalog curation rules, settings conversion rules, or alternative state machines. `SessionService` remains the single source of truth for create, start, stop, restart, and remove semantics, `BlockCatalogService` owns read-only catalog lookup behavior, and `BlockSettingsService` owns live runtime settings behavior.

## CLI Client

`gr4cp-cli` is a thin client for the REST API, not a second control plane. It reads local GRC files for `sessions create`, sends inline content to the existing HTTP server, and renders server responses or errors for an operator. It must not reimplement lifecycle rules, runtime behavior, or any business logic that belongs in `SessionService`.

## Block Catalog

The block catalog is intentionally separate from the session lifecycle. Its purpose is to expose stable, read-only metadata that a future `gr4-studio` UI can browse without depending on running sessions or runtime inspection.

The catalog slice uses a provider/service seam:

- `domain`: block descriptor, port descriptor, and parameter descriptor types
- `catalog`: `BlockCatalogProvider` and a GR4-backed `Gr4BlockCatalogProvider`
- `app`: `BlockCatalogService` for one-time snapshot caching plus deterministic list/get behavior

The preferred provider is tightly coupled to GNU Radio 4 itself:

- load plugins through GR4's plugin loader
- enumerate block types from the GR4 registry
- instantiate block models through GR4
- reflect ports and settings directly from `BlockModel`
- translate that metadata into this repo's stable `BlockDescriptor` shape

This repo intentionally does not import the old multi-layer plugin services, diagnostics surfaces, subprocess reflection paths, or graph validation architecture. GR4 is the source of truth, and this code only performs a thin translation into the API shape.

`BlockCatalogService` caches the provider snapshot in memory on first use. There is no refresh endpoint or rescan loop in this MVP; restart the process to reload the catalog. Server startup forces the initial load so a broken GR4 plugin or reflection environment fails fast instead of serving placeholder metadata.

Current mapping compromises:

- block summaries default to an empty string when GR4's type-erased reflection path does not expose a stable description field
- parameter `required` flags default to `false` because GR4's runtime reflection path does not expose that distinction cleanly here
- a small set of GR4 framework/base-block settings is filtered out so the API exposes user-facing block parameters rather than transport/runtime internals

The catalog must remain read-only metadata. It must not depend on runtime graph inspection, session state, or running graphs.

## Live Block Settings

Studio needs one narrow runtime control surface for widgets such as sliders, toggles, and numeric inputs. The control plane exposes that surface as settings-oriented HTTP endpoints instead of a generic runtime message bus:

- `POST /sessions/{id}/blocks/{unique_name}/settings`
- `GET /sessions/{id}/blocks/{unique_name}/settings`

These endpoints are session-scoped and runtime-only:

- they address a block by its running GR4 `unique_name`
- they require the session to be in `Running`
- they return conflict errors for non-running sessions
- they do not inspect stored GRC data to fabricate values

`POST /settings` accepts a plain JSON object and translates it into a GNU Radio 4 property message:

- default mode: `Set + StagedSettings`
- optional immediate mode: `Set + Settings`

The HTTP response records the applied mode as `staged_settings` or `settings`, and the request body must be a JSON object. Supported JSON values are `null`, boolean, integer, floating point, string, and nested objects; arrays are rejected.

`GET /settings` translates into:

- `Get + Settings`

The HTTP response wraps the current runtime settings under a top-level `settings` key.

The runtime path stays narrow and production-aligned:

- HTTP parses and serializes only
- `BlockSettingsService` validates session state and payload shape
- `RuntimeManager` owns GR4 interaction
- `Gr4RuntimeManager` uses the scheduler's built-in message routing to send the request to the target block and wait for the reply

Payload conversion is intentionally strict:

- supported request values: `null`, boolean, integer, floating point, string, nested objects
- arrays are rejected for now
- nested JSON objects map to nested `property_map`
- unsupported runtime reply value shapes are reported as errors

## Session Lifecycle Concept

The control plane will manage a small lifecycle for a session:

- stopped
- running
- error

Each `Session` stores:

- `id`
- `name`
- `grc_content`
- `state`
- optional `last_error`
- `created_at`
- `updated_at`

Lifecycle semantics are owned by `SessionService`:

- `create` validates non-empty GRC content and stores a new session in `Stopped`
- `start` prepares and starts the runtime, then marks the session `Running`
- `stop` is a no-op success when the session is already `Stopped`
- `restart` stops a running session first, then always destroys, prepares, and starts again
- `remove` stops a running session first, then destroys runtime state and deletes the session

If runtime operations fail during start, stop, restart, or remove, the service records the session as `Error`, stores `last_error`, updates `updated_at`, persists that state, and then throws an application-level runtime error.

## Runtime Seam

The runtime layer exists to isolate session lifecycle logic from GNU Radio execution details. The current `StubRuntimeManager` succeeds by default and only provides the control-plane seam needed by `SessionService`:

- `prepare`
- `start`
- `stop`
- `destroy`
- `set_block_settings`
- `get_block_settings`

This keeps runtime-specific concerns out of storage, domain, and HTTP while preserving a clean insertion point for later GR4 integration.

## In-Memory Repository Choice

The MVP uses a thread-safe in-memory repository backed by `std::unordered_map<std::string, Session>` and a single `std::mutex`. This is sufficient for the current appliance scope:

- no persistence
- no distributed coordination
- no extra repository abstractions beyond CRUD needed by `SessionService`

## API Contract

The only intended public API for this MVP is these 11 product endpoints:

- `POST /sessions`
- `GET /sessions`
- `GET /sessions/{id}`
- `DELETE /sessions/{id}`
- `POST /sessions/{id}/start`
- `POST /sessions/{id}/stop`
- `POST /sessions/{id}/restart`
- `POST /sessions/{id}/blocks/{unique_name}/settings`
- `GET /sessions/{id}/blocks/{unique_name}/settings`
- `GET /blocks`
- `GET /blocks/{id}`

No other product endpoints are allowed. A temporary bootstrap endpoint, `GET /healthz`, is allowed during bring-up.

## Out Of Scope

- Platform capabilities beyond a local session runner
- Graph modeling or graph editing abstractions
- Diagnostics, history, audit trails, or observability APIs
- Server-sent events, websockets, or other push channels
- Runtime-driven catalog generation or graph inspection
- Generic graph editing or parameter patching APIs beyond the narrow live block settings surface
- Compatibility layers for older designs
