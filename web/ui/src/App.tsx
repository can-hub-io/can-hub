import { useState, type ReactNode } from 'react'
import {
  api,
  type AclLevel,
  type Agent,
  type Client,
  type IfconfigOp,
  type Peer,
} from './api'
import { usePolling, useTelemetry } from './hooks'
import './App.css'

type Tab = 'dashboard' | 'peers' | 'agents' | 'clients' | 'interfaces' | 'pins' | 'acls'

const TABS: [Tab, string][] = [
  ['dashboard', 'Dashboard'],
  ['peers', 'Peers'],
  ['agents', 'Agents'],
  ['clients', 'Clients'],
  ['interfaces', 'Interfaces'],
  ['pins', 'Pins'],
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
        {tab === 'pins' && <Pins />}
        {tab === 'acls' && <Acls />}
      </main>
    </div>
  )
}

// Run a mutating action, reporting failures and refreshing on success.
async function runAction(action: () => Promise<void>, refresh: () => void) {
  try {
    await action()
    refresh()
  } catch (cause) {
    alert(cause instanceof Error ? cause.message : String(cause))
  }
}

const peerId = (id: number) => `0x${id.toString(16)}`
const shortFp = (fp: string) => (fp === '*' ? '* (any)' : fp.length > 16 ? fp.slice(0, 16) + '…' : fp)

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

interface Column<T> {
  header: string
  render: (row: T) => ReactNode
  num?: boolean
}

function DataView<T>({ fetcher, columns, rowKey, actions }: {
  fetcher: () => Promise<T[]>
  columns: Column<T>[]
  rowKey: (row: T) => string | number
  actions?: (row: T, refresh: () => void) => ReactNode
}) {
  const { data, error, refresh } = usePolling(fetcher)
  if (error) return <p className="error">{error}</p>
  if (!data) return <p>Loading…</p>
  if (data.length === 0) return <p>None.</p>
  return (
    <table>
      <thead>
        <tr>
          {columns.map((c) => <th key={c.header} className={c.num ? 'num' : ''}>{c.header}</th>)}
          {actions && <th></th>}
        </tr>
      </thead>
      <tbody>
        {data.map((row) => (
          <tr key={rowKey(row)}>
            {columns.map((c) => <td key={c.header} className={c.num ? 'num' : ''}>{c.render(row)}</td>)}
            {actions && <td className="num">{actions(row, refresh)}</td>}
          </tr>
        ))}
      </tbody>
    </table>
  )
}

function Peers() {
  return (
    <DataView<Peer>
      fetcher={api.peers}
      rowKey={(r) => r.peerId}
      columns={[
        { header: 'Peer', render: (r) => peerId(r.peerId) },
        { header: 'Role', render: (r) => r.role },
        { header: 'Agent', render: (r) => r.agentName || '—' },
        { header: 'Fingerprint', render: (r) => (r.fingerprintHex ? shortFp(r.fingerprintHex) : '—') },
        { header: 'Forwarded', render: (r) => r.framesForwarded.toLocaleString(), num: true },
        { header: 'Dropped', render: (r) => r.framesDropped.toLocaleString(), num: true },
      ]}
      actions={(r, refresh) => (
        <button onClick={() => runAction(() => api.kickPeer(r.peerId), refresh)}>Kick</button>
      )}
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
        { header: 'Fingerprint', render: (r) => (r.fingerprintHex ? shortFp(r.fingerprintHex) : '—') },
      ]}
      actions={(r, refresh) => (
        <button onClick={() => runAction(() => api.kickAgent(r.agentName), refresh)}>Kick</button>
      )}
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
        { header: 'Interface', render: (r) => (r.agentName ? `${r.agentName}/${r.interfaceName}` : '—') },
        { header: 'Channel', render: (r) => r.channel ?? 'idle' },
        { header: 'Forwarded', render: (r) => r.framesForwarded.toLocaleString(), num: true },
        { header: 'Dropped', render: (r) => r.framesDropped.toLocaleString(), num: true },
      ]}
    />
  )
}

function Interfaces() {
  const { data, error, refresh } = usePolling(api.interfaces)
  const [selected, setSelected] = useState('')
  const [op, setOp] = useState<IfconfigOp>('bitrate')
  const [bitrate, setBitrate] = useState(500000)

  const apply = () => {
    const iface = (data ?? []).find((i) => `${i.agentName}/${i.interfaceName}` === selected)
    if (!iface) return alert('select an interface')
    runAction(() => api.interfaceConfig(iface.agentName, iface.interfaceName, op, bitrate), refresh)
  }

  return (
    <section>
      {error && <p className="error">{error}</p>}
      <div className="form">
        <select value={selected} onChange={(e) => setSelected(e.target.value)}>
          <option value="">interface…</option>
          {(data ?? []).map((i) => {
            const name = `${i.agentName}/${i.interfaceName}`
            return <option key={i.interfaceId} value={name}>{name}</option>
          })}
        </select>
        <select value={op} onChange={(e) => setOp(e.target.value as IfconfigOp)}>
          <option value="bitrate">set bitrate</option>
          <option value="up">link up</option>
          <option value="down">link down</option>
        </select>
        {op === 'bitrate' && (
          <input type="number" value={bitrate} onChange={(e) => setBitrate(Number(e.target.value))} style={{ width: 110 }} />
        )}
        <button onClick={apply}>Apply</button>
      </div>

      <table>
        <thead>
          <tr><th>Interface</th><th className="num">Subscribers</th><th className="num">Frames</th></tr>
        </thead>
        <tbody>
          {(data ?? []).map((i) => (
            <tr key={i.interfaceId}>
              <td>{i.agentName}/{i.interfaceName}</td>
              <td className="num">{i.subscriberCount}</td>
              <td className="num">{i.framesReceived.toLocaleString()}</td>
            </tr>
          ))}
          {data && data.length === 0 && <tr><td colSpan={3}>None.</td></tr>}
        </tbody>
      </table>
    </section>
  )
}

function Pins() {
  const { data, error, refresh } = usePolling(api.pins)
  const [name, setName] = useState('')
  const [fingerprint, setFingerprint] = useState('')

  const add = () => {
    if (!name || !fingerprint) return alert('name and fingerprint required')
    runAction(async () => {
      await api.pinAdd(name, fingerprint)
      setName('')
      setFingerprint('')
    }, refresh)
  }

  return (
    <section>
      {error && <p className="error">{error}</p>}
      <div className="form">
        <input placeholder="agent name" value={name} onChange={(e) => setName(e.target.value)} />
        <input placeholder="fingerprint (sha256 hex)" value={fingerprint} onChange={(e) => setFingerprint(e.target.value)} style={{ minWidth: 280 }} />
        <button onClick={add}>Add pin</button>
      </div>

      <table>
        <thead><tr><th>Agent</th><th>Fingerprint</th><th></th></tr></thead>
        <tbody>
          {(data ?? []).map((p) => (
            <tr key={p.agentName}>
              <td>{p.agentName}</td>
              <td>{shortFp(p.fingerprintHex)}</td>
              <td className="num">
                <button onClick={() => runAction(() => api.pinDelete(p.agentName), refresh)}>Delete</button>
              </td>
            </tr>
          ))}
          {data && data.length === 0 && <tr><td colSpan={3}>None.</td></tr>}
        </tbody>
      </table>
    </section>
  )
}

function Acls() {
  const { data, error, refresh } = usePolling(api.acls)
  const [fingerprint, setFingerprint] = useState('*')
  const [agent, setAgent] = useState('*')
  const [iface, setIface] = useState('*')
  const [level, setLevel] = useState<AclLevel>('ro')

  const grant = () => {
    runAction(() => api.aclSet(fingerprint || '*', agent || '*', iface || '*', level), refresh)
  }

  return (
    <section>
      {error && <p className="error">{error}</p>}
      <div className="form">
        <input placeholder="fingerprint or *" value={fingerprint} onChange={(e) => setFingerprint(e.target.value)} style={{ minWidth: 220 }} />
        <input placeholder="agent or *" value={agent} onChange={(e) => setAgent(e.target.value)} />
        <input placeholder="iface or *" value={iface} onChange={(e) => setIface(e.target.value)} />
        <select value={level} onChange={(e) => setLevel(e.target.value as AclLevel)}>
          <option value="none">none</option>
          <option value="ro">ro</option>
          <option value="rw">rw</option>
        </select>
        <button onClick={grant}>Grant</button>
      </div>

      <table>
        <thead><tr><th>Fingerprint</th><th>Object</th><th>Level</th><th></th></tr></thead>
        <tbody>
          {(data ?? []).map((a) => (
            <tr key={`${a.fingerprintHex}-${a.agentName}-${a.interfaceName}`}>
              <td>{shortFp(a.fingerprintHex)}</td>
              <td>{a.agentName}/{a.interfaceName}</td>
              <td>{a.level}</td>
              <td className="num">
                <button onClick={() => runAction(() => api.aclRevoke(a.fingerprintHex, a.agentName, a.interfaceName), refresh)}>
                  Revoke
                </button>
              </td>
            </tr>
          ))}
          {data && data.length === 0 && <tr><td colSpan={4}>None.</td></tr>}
        </tbody>
      </table>
    </section>
  )
}

export default App
