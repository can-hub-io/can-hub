import { api } from '../api'
import { usePolling, useTelemetry } from '../hooks'

export function Dashboard() {
  const { frame, connected } = useTelemetry()
  const { data: status, error } = usePolling(api.status)

  const cards: [string, string][] = [
    ['Peers', status ? String(status.peerCount) : '—'],
    ['Agents', status ? String(status.agentCount) : '—'],
    ['Clients', status ? String(status.clientCount) : '—'],
    ['Interfaces', status ? String(status.interfaceCount) : '—'],
  ]
  const rates: [string, number | undefined][] = [
    ['Received /s', frame?.rates.receivedPerS],
    ['Forwarded /s', frame?.rates.forwardedPerS],
    ['Dropped /s', frame?.rates.droppedPerS],
    ['Unroutable /s', frame?.rates.unroutablePerS],
  ]

  return (
    <section>
      {error && <p className="error">{error}</p>}
      <div className="cards">
        {cards.map(([label, value]) => (
          <div className="card" key={label}>
            <span className="card-value">{value}</span>
            <span className="card-label">{label}</span>
          </div>
        ))}
      </div>

      <h2>
        Live rates <span className={connected ? 'dot live' : 'dot'} title={connected ? 'connected' : 'offline'} />
      </h2>
      <div className="cards">
        {rates.map(([label, value]) => (
          <div className="card" key={label}>
            <span className="card-value">{value === undefined ? '—' : value.toFixed(1)}</span>
            <span className="card-label">{label}</span>
          </div>
        ))}
      </div>

      <h2>Per-interface throughput</h2>
      <table>
        <thead>
          <tr><th>Interface</th><th className="num">Frames</th><th className="num">Frames /s</th></tr>
        </thead>
        <tbody>
          {(frame?.interfaces ?? []).map((row) => (
            <tr key={row.interfaceId}>
              <td>{row.agentName}/{row.interfaceName}</td>
              <td className="num">{row.framesReceived.toLocaleString()}</td>
              <td className="num">{row.framesPerS.toFixed(1)}</td>
            </tr>
          ))}
          {!frame && <tr><td colSpan={3}>Waiting for telemetry…</td></tr>}
        </tbody>
      </table>
    </section>
  )
}
