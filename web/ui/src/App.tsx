import { useState, type ReactNode } from 'react'
import {
  api,
  PERMISSION,
  type AclLevel,
  type Agent,
  type AuthState,
  type Client,
  type IfconfigOp,
  type Peer,
} from './api'
import { useAuth, usePolling, useTelemetry } from './hooks'
import './App.css'

type Tab = 'dashboard' | 'peers' | 'agents' | 'clients' | 'interfaces' | 'pins' | 'acls' | 'users' | 'audit'

// Each tab requires a permission to appear.
const TABS: [Tab, string, string][] = [
  ['dashboard', 'Dashboard', PERMISSION.viewsRead],
  ['peers', 'Peers', PERMISSION.viewsRead],
  ['agents', 'Agents', PERMISSION.viewsRead],
  ['clients', 'Clients', PERMISSION.viewsRead],
  ['interfaces', 'Interfaces', PERMISSION.viewsRead],
  ['pins', 'Pins', PERMISSION.pinsManage],
  ['acls', 'ACLs', PERMISSION.aclManage],
  ['users', 'Users', PERMISSION.usersManage],
  ['audit', 'Audit', PERMISSION.usersManage],
]

function App() {
  const { state, reload } = useAuth()
  if (!state) return <p style={{ padding: '2rem' }}>Loading…</p>
  if (state.needsBootstrap) return <Bootstrap onDone={reload} />
  if (!state.authenticated) return <Login onDone={reload} />
  return <Console auth={state} onLogout={reload} />
}

function Bootstrap({ onDone }: { onDone: () => void }) {
  return (
    <AuthForm
      title="Create the first admin"
      submitLabel="Create admin"
      action={(name, password) => api.setup(name, password)}
      onDone={onDone}
    />
  )
}

function Login({ onDone }: { onDone: () => void }) {
  return (
    <AuthForm title="can-hub admin" submitLabel="Sign in" action={(name, password) => api.login(name, password)} onDone={onDone} />
  )
}

function AuthForm({ title, submitLabel, action, onDone }: {
  title: string
  submitLabel: string
  action: (name: string, password: string) => Promise<AuthState>
  onDone: () => void
}) {
  const [name, setName] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError] = useState<string | null>(null)

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    try {
      await action(name, password)
      onDone()
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  return (
    <div className="app" style={{ maxWidth: 360 }}>
      <h1>{title}</h1>
      <form className="form" onSubmit={submit} style={{ flexDirection: 'column', alignItems: 'stretch' }}>
        <input placeholder="user" value={name} onChange={(e) => setName(e.target.value)} autoFocus />
        <input placeholder="password" type="password" value={password} onChange={(e) => setPassword(e.target.value)} />
        {error && <p className="error">{error}</p>}
        <button type="submit">{submitLabel}</button>
      </form>
    </div>
  )
}

function Console({ auth, onLogout }: { auth: AuthState; onLogout: () => void }) {
  const allowed = TABS.filter(([, , permission]) => auth.permissions.includes(permission))
  const [tab, setTab] = useState<Tab>(allowed[0]?.[0] ?? 'dashboard')

  const logout = async () => {
    await api.logout()
    onLogout()
  }

  return (
    <div className="app">
      <header>
        <div className="topbar">
          <h1>can-hub admin</h1>
          <span className="who">
            {auth.user} <button onClick={logout}>Sign out</button>
          </span>
        </div>
        <nav>
          {allowed.map(([id, label]) => (
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
        {tab === 'users' && <Users />}
        {tab === 'audit' && <Audit />}
      </main>
    </div>
  )
}

function Audit() {
  const { data, error } = usePolling(api.listAudit, 5000)
  if (error) return <p className="error">{error}</p>
  if (!data) return <p>Loading…</p>
  if (data.length === 0) return <p>No audited actions yet.</p>
  return (
    <table>
      <thead>
        <tr><th>When</th><th>Actor</th><th>Action</th><th>Target</th><th className="num">Status</th></tr>
      </thead>
      <tbody>
        {data.map((entry, index) => (
          <tr key={index}>
            <td>{new Date(entry.at * 1000).toLocaleString()}</td>
            <td>{entry.actor}</td>
            <td>{entry.action}</td>
            <td>{entry.target}</td>
            <td className="num">{entry.status}</td>
          </tr>
        ))}
      </tbody>
    </table>
  )
}

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

function Users() {
  const users = usePolling(api.listManagedUsers)
  const groups = usePolling(api.listManagedGroups)
  const permissions = usePolling(api.listPermissions)
  const [userName, setUserName] = useState('')
  const [userPassword, setUserPassword] = useState('')
  const [groupName, setGroupName] = useState('')

  const refreshAll = () => {
    users.refresh()
    groups.refresh()
  }

  const addUser = () => {
    if (!userName || !userPassword) return alert('name and password required')
    runAction(async () => {
      await api.createManagedUser(userName, userPassword)
      setUserName('')
      setUserPassword('')
    }, refreshAll)
  }

  const addGroup = () => {
    if (!groupName) return alert('group name required')
    runAction(async () => {
      await api.createManagedGroup(groupName)
      setGroupName('')
    }, refreshAll)
  }

  const groupList = groups.data ?? []
  const permissionList = permissions.data ?? []

  return (
    <section>
      <h2>Groups</h2>
      <div className="form">
        <input placeholder="group name" value={groupName} onChange={(e) => setGroupName(e.target.value)} />
        <button onClick={addGroup}>Add group</button>
      </div>
      <table>
        <thead>
          <tr><th>Group</th>{permissionList.map((p) => <th key={p}>{p}</th>)}<th></th></tr>
        </thead>
        <tbody>
          {groupList.map((g) => (
            <tr key={g.id}>
              <td>{g.name}</td>
              {permissionList.map((p) => {
                const has = g.permissions.includes(p)
                const next = has ? g.permissions.filter((x) => x !== p) : [...g.permissions, p]
                return (
                  <td key={p} className="num">
                    <input
                      type="checkbox"
                      checked={has}
                      onChange={() => runAction(() => api.setGroupPermissions(g.id, next), refreshAll)}
                    />
                  </td>
                )
              })}
              <td className="num">
                <button onClick={() => runAction(() => api.deleteManagedGroup(g.id), refreshAll)}>Delete</button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>

      <h2>Users</h2>
      <div className="form">
        <input placeholder="user name" value={userName} onChange={(e) => setUserName(e.target.value)} />
        <input placeholder="password" type="password" value={userPassword} onChange={(e) => setUserPassword(e.target.value)} />
        <button onClick={addUser}>Add user</button>
      </div>
      <table>
        <thead>
          <tr><th>User</th><th>Enabled</th>{groupList.map((g) => <th key={g.id}>{g.name}</th>)}<th></th></tr>
        </thead>
        <tbody>
          {(users.data ?? []).map((u) => (
            <tr key={u.id}>
              <td>{u.name}</td>
              <td className="num">
                <input
                  type="checkbox"
                  checked={u.enabled}
                  onChange={() => runAction(() => api.setManagedUserEnabled(u.id, !u.enabled), refreshAll)}
                />
              </td>
              {groupList.map((g) => {
                const member = u.groupIds.includes(g.id)
                return (
                  <td key={g.id} className="num">
                    <input
                      type="checkbox"
                      checked={member}
                      onChange={() =>
                        runAction(
                          () => (member ? api.removeMembership(u.id, g.id) : api.addMembership(u.id, g.id)),
                          refreshAll,
                        )
                      }
                    />
                  </td>
                )
              })}
              <td className="num">
                <button onClick={() => runAction(() => api.deleteManagedUser(u.id), refreshAll)}>Delete</button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </section>
  )
}

export default App
