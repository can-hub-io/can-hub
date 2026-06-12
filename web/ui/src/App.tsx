import { useState, type ReactNode } from 'react'
import { api, type Acl, type Agent, type Client, type Interface, type Peer } from './api'
import { usePolling, useTelemetry } from './hooks'
import './App.css'

type Tab = 'dashboard' | 'peers' | 'agents' | 'clients' | 'interfaces' | 'acls'

const TABS: [Tab, string][] = [
  ['dashboard', 'Dashboard'],
  ['peers', 'Peers'],
  ['agents', 'Agents'],
  ['clients', 'Clients'],
  ['interfaces', 'Interfaces'],
  ['acls', 'ACLs'],
]

function App() {
  const [tab, setTab] = useState<Tab>('dashboard')
  return (
    <div className="app">
      <header>
        <h1>can-hub admin</h1>
        <nav>
          {TABS.map(([id, label]) => (
            <button key={id} className={id === tab ? 'active' : ''} onClick={() => setTab(id)}>
              {label}
            </button>
          ))}
        </nav>
      </header>
      <main>
        {tab === 'dashboard' && <Dashboard />}
        {tab === 'peers' && <Peers />}
        {tab === 'agents' && <Agents />}
        {tab === 'clients' && <Clients />}
        {tab === 'interfaces' && <Interfaces />}
        {tab === 'acls' && <Acls />}
      </main>
    </div>
  )
}

function Dashboard() {
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

function DataView<T>({ fetcher, columns, rowKey }: {
  fetcher: () => Promise<T[]>
  columns: { header: string; render: (row: T) => ReactNode; num?: boolean }[]
  rowKey: (row: T) => string | number
}) {
  const { data, error } = usePolling(fetcher)
  if (error) return <p className="error">{error}</p>
  if (!data) return <p>Loading…</p>
  if (data.length === 0) return <p>None.</p>
  return (
    <table>
      <thead>
        <tr>{columns.map((c) => <th key={c.header} className={c.num ? 'num' : ''}>{c.header}</th>)}</tr>
      </thead>
      <tbody>
        {data.map((row) => (
          <tr key={rowKey(row)}>
            {columns.map((c) => <td key={c.header} className={c.num ? 'num' : ''}>{c.render(row)}</td>)}
          </tr>
        ))}
      </tbody>
    </table>
  )
}

const peerId = (id: number) => `0x${id.toString(16)}`

function Peers() {
  return (
    <DataView<Peer>
      fetcher={api.peers}
      rowKey={(r) => r.peerId}
      columns={[
        { header: 'Peer', render: (r) => peerId(r.peerId) },
        { header: 'Role', render: (r) => r.role },
        { header: 'Agent', render: (r) => r.agentName || '—' },
        { header: 'Fingerprint', render: (r) => r.fingerprintHex ? r.fingerprintHex.slice(0, 16) + '…' : '—' },
        { header: 'Forwarded', render: (r) => r.framesForwarded.toLocaleString(), num: true },
        { header: 'Dropped', render: (r) => r.framesDropped.toLocaleString(), num: true },
      ]}
    />
  )
}

function Agents() {
  return (
    <DataView<Agent>
      fetcher={api.agents}
      rowKey={(r) => r.peerId}
      columns={[
        { header: 'Agent', render: (r) => r.agentName },
        { header: 'Peer', render: (r) => peerId(r.peerId) },
        { header: 'Interfaces', render: (r) => r.interfaceCount, num: true },
        { header: 'Fingerprint', render: (r) => r.fingerprintHex ? r.fingerprintHex.slice(0, 16) + '…' : '—' },
      ]}
    />
  )
}

function Clients() {
  return (
    <DataView<Client>
      fetcher={api.clients}
      rowKey={(r) => `${r.peerId}-${r.interfaceId}-${r.channel}`}
      columns={[
        { header: 'Peer', render: (r) => peerId(r.peerId) },
        { header: 'Interface', render: (r) => r.agentName ? `${r.agentName}/${r.interfaceName}` : '—' },
        { header: 'Channel', render: (r) => r.channel ?? 'idle' },
        { header: 'Forwarded', render: (r) => r.framesForwarded.toLocaleString(), num: true },
        { header: 'Dropped', render: (r) => r.framesDropped.toLocaleString(), num: true },
      ]}
    />
  )
}

function Interfaces() {
  return (
    <DataView<Interface>
      fetcher={api.interfaces}
      rowKey={(r) => r.interfaceId}
      columns={[
        { header: 'Interface', render: (r) => `${r.agentName}/${r.interfaceName}` },
        { header: 'Subscribers', render: (r) => r.subscriberCount, num: true },
        { header: 'Frames', render: (r) => r.framesReceived.toLocaleString(), num: true },
      ]}
    />
  )
}

function Acls() {
  return (
    <DataView<Acl>
      fetcher={api.acls}
      rowKey={(r) => `${r.fingerprintHex}-${r.agentName}-${r.interfaceName}`}
      columns={[
        { header: 'Fingerprint', render: (r) => r.fingerprintHex === '*' ? '* (any)' : r.fingerprintHex.slice(0, 16) + '…' },
        { header: 'Object', render: (r) => `${r.agentName}/${r.interfaceName}` },
        { header: 'Level', render: (r) => r.level },
      ]}
    />
  )
}

export default App
