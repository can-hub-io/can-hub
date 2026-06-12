import { useEffect, useState } from 'react'

// Mirrors the daemon StatusDto (GET /api/status). Real-time rates arrive over
// WebSocket in a later phase; this is the Phase 1 polling skeleton.
interface Status {
  peerCount: number
  agentCount: number
  clientCount: number
  interfaceCount: number
  framesReceived: number
  framesForwarded: number
  framesDropped: number
  framesUnroutable: number
}

const COUNTERS: [keyof Status, string][] = [
  ['peerCount', 'Peers'],
  ['agentCount', 'Agents'],
  ['clientCount', 'Clients'],
  ['interfaceCount', 'Interfaces'],
  ['framesReceived', 'Frames received'],
  ['framesForwarded', 'Frames forwarded'],
  ['framesDropped', 'Frames dropped'],
  ['framesUnroutable', 'Frames unroutable'],
]

function App() {
  const [status, setStatus] = useState<Status | null>(null)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    let active = true
    const load = async () => {
      try {
        const response = await fetch('/api/status')
        if (!response.ok) throw new Error(`hub unreachable (${response.status})`)
        const body: Status = await response.json()
        if (active) {
          setStatus(body)
          setError(null)
        }
      } catch (cause) {
        if (active) setError(cause instanceof Error ? cause.message : String(cause))
      }
    }
    load()
    const timer = setInterval(load, 2000)
    return () => {
      active = false
      clearInterval(timer)
    }
  }, [])

  return (
    <main style={{ fontFamily: 'system-ui, sans-serif', maxWidth: 640, margin: '2rem auto', padding: '0 1rem' }}>
      <h1>can-hub admin</h1>
      {error && <p style={{ color: '#b00' }}>{error}</p>}
      {!status && !error && <p>Loading…</p>}
      {status && (
        <table style={{ borderCollapse: 'collapse', width: '100%' }}>
          <tbody>
            {COUNTERS.map(([key, label]) => (
              <tr key={key}>
                <td style={{ padding: '0.4rem 0.8rem', borderBottom: '1px solid #ddd' }}>{label}</td>
                <td style={{ padding: '0.4rem 0.8rem', borderBottom: '1px solid #ddd', textAlign: 'right', fontVariantNumeric: 'tabular-nums' }}>
                  {status[key].toLocaleString()}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </main>
  )
}

export default App
