# can-hub-web UI

React + TypeScript + Vite SPA for the can-hub web admin panel, served by
`web/daemon` (embedded in release builds via the `embed-ui` cargo feature).

```sh
npm install
npm run dev        # Vite dev server; proxies /api + WebSocket to 127.0.0.1:8080
npm run build      # production bundle into dist/
npm run lint
```

Run `web/daemon` locally (`cargo run`) so the proxy has a backend. Styling
is Tailwind with shadcn-style components. The REST and WebSocket contracts
live in [`web/openapi.yaml`](../openapi.yaml); development workflow in
[web/README.md](../README.md).
