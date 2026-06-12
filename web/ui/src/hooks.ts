import { useCallback, useEffect, useRef, useState } from 'react'
import { api, telemetryUrl, type AuthState, type TelemetryFrame } from './api'

// Current auth state (bootstrap needed / authenticated / permissions).
export function useAuth() {
  const [state, setState] = useState<AuthState | null>(null)
  const reload = useCallback(async () => {
    try {
      setState(await api.authState())
    } catch {
      setState({ needsBootstrap: false, authenticated: false, user: null, permissions: [], csrfToken: null })
    }
  }, [])
  useEffect(() => {
    reload()
  }, [reload])
  return { state, reload }
}

// Poll a fetcher on an interval, exposing the latest data and any error.
export function usePolling<T>(fetcher: () => Promise<T>, intervalMs = 2000) {
  const [data, setData] = useState<T | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [version, setVersion] = useState(0)
  const fetcherRef = useRef(fetcher)
  fetcherRef.current = fetcher

  useEffect(() => {
    let active = true
    const load = async () => {
      try {
        const result = await fetcherRef.current()
        if (active) {
          setData(result)
          setError(null)
        }
      } catch (cause) {
        if (active) setError(cause instanceof Error ? cause.message : String(cause))
      }
    }
    load()
    const timer = setInterval(load, intervalMs)
    return () => {
      active = false
      clearInterval(timer)
    }
  }, [intervalMs, version])

  // Trigger an immediate reload (e.g. after a mutating action).
  const refresh = () => setVersion((v) => v + 1)
  return { data, error, refresh }
}

// Subscribe to the telemetry WebSocket, reconnecting if it drops. Returns the
// latest frame and whether the socket is currently connected.
export function useTelemetry() {
  const [frame, setFrame] = useState<TelemetryFrame | null>(null)
  const [connected, setConnected] = useState(false)

  useEffect(() => {
    let closed = false
    let socket: WebSocket | null = null
    let retry: ReturnType<typeof setTimeout> | undefined

    const connect = () => {
      socket = new WebSocket(telemetryUrl())
      socket.onopen = () => setConnected(true)
      socket.onmessage = (event) => {
        try {
          setFrame(JSON.parse(event.data) as TelemetryFrame)
        } catch {
          // ignore malformed frame
        }
      }
      socket.onclose = () => {
        setConnected(false)
        if (!closed) retry = setTimeout(connect, 2000)
      }
      socket.onerror = () => socket?.close()
    }
    connect()

    return () => {
      closed = true
      if (retry) clearTimeout(retry)
      socket?.close()
    }
  }, [])

  return { frame, connected }
}
