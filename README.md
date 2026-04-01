# gr4-control-plane

Minimal C++23 REST control plane for GNU Radio 4 sessions. This repository was formerly `gr4-control-plane-mvp`.

This codebase is intentionally narrow:

- single-process appliance
- session lifecycle plus a narrow live block-settings surface
- GR4-backed read-only block catalog
- one thin HTTP layer over application services
- one thin CLI client over the REST API
- no broader platform features

The core application logic lives in the `domain`, `storage`, `runtime`, and `app` layers. The `api` layer is intentionally thin and only translates HTTP into service calls, serialization, and error mapping.

The repository also includes `gr4cp-cli`, a thin client that talks to the REST API. It does not implement any control-plane logic locally. See [`docs/design.md`](docs/design.md) for the architecture notes.

## Build

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The block catalog is GNU Radio 4-backed only. `GR4CP_ENABLE_GR4_CATALOG=ON` is the normal build mode, and configuration requires a working `gnuradio4` installation. Server startup validates plugin loading and reflection up front; if the GR4 catalog cannot be initialized, startup fails and the server does not run.

To point CMake at a non-standard GNU Radio 4 install:

```bash
cmake -S . -B build -DGR4CP_GNURADIO4_PREFIX=/path/to/gr4/prefix
```

Built executables:

- `build/gr4cp_server`
- `build/gr4cp-cli`

## Docker Images

The repository now uses three Docker images:

- `ghcr.io/<owner>/gnuradio4-sdk:latest`
  - GNU Radio 4 base SDK image
  - built from the `gnuradio4-sdk` target in [`Dockerfile`](Dockerfile)
  - includes GNU Radio 4 and `gr4-incubator`
  - intended as the shared base for this repo and downstream OOT builds
- `ghcr.io/<owner>/gr4-control-plane-sdk:latest`
  - control-plane SDK image
  - built from the `sdk` target in [`Dockerfile`](Dockerfile)
  - includes the installed `gr4-control-plane` surface on top of `gnuradio4-sdk`
- `ghcr.io/<owner>/gr4-control-plane-runtime:latest`
  - lean runtime image
  - built from the `runtime` target in [`Dockerfile`](Dockerfile)

All three images are published as multi-arch manifests for `linux/amd64` and `linux/arm64`, so they can run on both standard Linux hosts and Apple Silicon.

The SDK image can be used as a `FROM` base in downstream block repositories. For example:

```dockerfile
FROM ghcr.io/<owner>/gnuradio4-sdk:latest

WORKDIR /workspace/my-oot
COPY . .
RUN cmake -S . -B build && cmake --build build -j"$(nproc)"
```

GitHub Actions behavior:

- `workflow_dispatch`
  - can rebuild and publish `gnuradio4-sdk:latest`
- pull requests
  - build and test the control-plane SDK image only
  - do not publish
- pushes to `main`
  - use `gnuradio4-sdk:latest`
  - build and publish `gr4-control-plane-sdk:latest`
  - build and publish `gr4-control-plane-runtime:latest`

## HTTP API

The public API consists of 11 product endpoints plus temporary `GET /healthz`:

- Sessions:
  - `POST /sessions`
  - `GET /sessions`
  - `GET /sessions/{id}`
  - `DELETE /sessions/{id}`
  - `POST /sessions/{id}/start`
  - `POST /sessions/{id}/stop`
  - `POST /sessions/{id}/restart`
- Live block settings:
  - `POST /sessions/{id}/blocks/{unique_name}/settings`
  - `GET /sessions/{id}/blocks/{unique_name}/settings`
- Block catalog:
  - `GET /blocks`
  - `GET /blocks/{id}`

Bootstrap-only endpoint:

- `GET /healthz`

`GET /healthz` is temporary infrastructure for bring-up and is not part of the product API.

### Create A Session

```bash
curl -X POST http://127.0.0.1:8080/sessions \
  -H 'Content-Type: application/json' \
  -d '{"name":"demo","grc":"<inline grc content>"}'
```

If `name` is omitted, the server stores an empty string.

### List Sessions

```bash
curl http://127.0.0.1:8080/sessions
```

### Start A Session

```bash
curl -X POST http://127.0.0.1:8080/sessions/<id>/start
```

### Stop A Session

```bash
curl -X POST http://127.0.0.1:8080/sessions/<id>/stop
```

### Restart A Session

```bash
curl -X POST http://127.0.0.1:8080/sessions/<id>/restart
```

### Delete A Session

```bash
curl -X DELETE http://127.0.0.1:8080/sessions/<id>
```

### Update Running Block Settings

`POST /sessions/{id}/blocks/{unique_name}/settings` applies a partial settings update to a running block addressed by its runtime `unique_name`.

```bash
curl -X POST http://127.0.0.1:8080/sessions/<id>/blocks/src0/settings \
  -H 'Content-Type: application/json' \
  -d '{"frequency":1250.0,"amplitude":0.5}'
```

Use `?mode=immediate` to request the immediate GR4 property endpoint instead of the default staged path.

This endpoint only operates on running sessions. It returns a conflict error for stopped or errored sessions and does not fabricate static values from stored GRC content.

Request bodies must be JSON objects. Supported JSON value shapes are `null`, boolean, integer, floating point, string, and nested objects. Arrays are rejected.

Successful responses include:

- `session_id`
- `block`
- `applied_via` (`staged_settings` or `settings`)
- `accepted`

### Read Running Block Settings

`GET /sessions/{id}/blocks/{unique_name}/settings` fetches the current effective runtime settings for a running block.

```bash
curl http://127.0.0.1:8080/sessions/<id>/blocks/src0/settings
```

The response is wrapped as:

```json
{
  "settings": {
    "frequency": 1250.0,
    "amplitude": 0.5
  }
}
```

## Block Catalog

The block catalog is a read-only metadata surface intended for future `gr4-studio` integration. GR4 plugin loading, registry enumeration, and block reflection are the source of truth for this metadata. The production server does not fall back to static catalog data.

- `GET /blocks`
- `GET /blocks/{id}`

This catalog is not runtime graph inspection and is not coupled to running sessions.

Examples:

```bash
curl http://127.0.0.1:8080/blocks
curl http://127.0.0.1:8080/blocks/blocks.math.add_ff
```

## CLI

The CLI is a small wrapper over the 7 session lifecycle endpoints. It talks to a running server and defaults to `http://127.0.0.1:8080`.

Supported commands:

- `gr4cp-cli sessions create --file path/to/graph.grc [--name demo] [--url http://127.0.0.1:8080]`
- `gr4cp-cli sessions list [--url http://127.0.0.1:8080]`
- `gr4cp-cli sessions get <id> [--url http://127.0.0.1:8080]`
- `gr4cp-cli sessions start <id> [--url http://127.0.0.1:8080]`
- `gr4cp-cli sessions stop <id> [--url http://127.0.0.1:8080]`
- `gr4cp-cli sessions restart <id> [--url http://127.0.0.1:8080]`
- `gr4cp-cli sessions delete <id> [--url http://127.0.0.1:8080]`

Examples:

```bash
./build/gr4cp-cli sessions create --file demo.grc --name demo
./build/gr4cp-cli sessions list
./build/gr4cp-cli sessions start sess_0123456789abcdef
./build/gr4cp-cli sessions delete sess_0123456789abcdef
```

## License

This project is licensed under the MIT License. See [`LICENSE`](/home/josh/github_altiolabs/gr4-dev/src/gr4-control-plane/LICENSE) for the full text.
