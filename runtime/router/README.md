# MiaoDesk L1 model router

Zero-dependency OpenAI-compatible gateway for the DGX/local-AI tier.

The Windows client keeps one profile:

```text
Base URL = http://<DGX>:8000/v1
Model    = miaodesk
```

The router chooses an upstream model without changing the client contract.

## Routing

Rules are intentionally ordered and fixed:

1. `tools` or an active `tool_choice` -> primary model (`primary-tools`)
2. stable MiaoDesk content-skill signature in system text -> primary model (`primary-skill`)
3. short request with no tools -> fast model (`fast-short`)
4. everything else -> primary model (`primary-chat`)

If the fast backend is unreachable or returns 5xx, the same request is retried once
against primary and the response header becomes
`x-miaodesk-route: fast-short-fallback-primary`.

4xx responses are not retried because a larger model cannot repair an invalid client request.

## Run

Requires Node.js with built-in `fetch` (Node 18+).

```bash
export MIAODESK_ROUTER_HOST=0.0.0.0
export MIAODESK_ROUTER_PORT=8000

export MIAODESK_PRIMARY_BASE_URL=http://127.0.0.1:8001/v1
export MIAODESK_PRIMARY_MODEL=your-primary-model

export MIAODESK_FAST_BASE_URL=http://127.0.0.1:8002/v1
export MIAODESK_FAST_MODEL=your-fast-model

node runtime/router/server.mjs
```

Optional variables:

- `MIAODESK_PRIMARY_API_KEY`
- `MIAODESK_FAST_API_KEY`
- `MIAODESK_ROUTER_API_KEY` — when set, clients must send `Authorization: Bearer <value>`
- `MIAODESK_ROUTER_PUBLIC_MODEL` — default `miaodesk`
- `MIAODESK_ROUTER_SHORT_USER_CHARS` — default 240
- `MIAODESK_ROUTER_SHORT_TOTAL_CHARS` — default 900

## Endpoints

- `GET /health`
- `GET /v1/health`
- `GET /v1/models`
- `GET /metrics` (also `/v1/metrics`)
- `POST /v1/chat/completions`

The gateway rewrites only the model id. It preserves messages, tools, tool_choice,
stream, temperature and the rest of the OpenAI-compatible request body.

Response diagnostics:

- `x-miaodesk-route`
- `x-miaodesk-upstream-model`

## Verify

```bash
node tests/local-ai-router.mjs
```

The test starts two real loopback HTTP backends plus the router and proves tools,
skill signatures, short/long requests, streaming and fast-backend fallback.

This router is DGX/local-infrastructure code. It is not staged into the Windows package.


## DGX smoke / latency report

After the real primary and fast model servers are attached:

```bash
MIAODESK_ROUTER_BASE_URL=http://127.0.0.1:8000/v1 \
MIAODESK_ROUTER_SMOKE_RUNS=3 \
node runtime/router/smoke.mjs
```

If the router requires inbound auth, also set `MIAODESK_ROUTER_API_KEY`.

The report exercises short / tools / content-skill / long-chat routes and prints
p50 / p95 / max latency, the actual route header, selected upstream model and the
router's current in-memory metrics. This is the first deployment-day evidence for P2-3;
it measures routing, not answer quality.
