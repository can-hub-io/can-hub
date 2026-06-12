import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// The production build lands in dist/ and is served by the Rust daemon; in
// dev, /api and /healthz are proxied to a locally running can-hub-web.
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/api': 'http://127.0.0.1:8080',
      '/healthz': 'http://127.0.0.1:8080',
    },
  },
  build: {
    outDir: 'dist',
  },
})
