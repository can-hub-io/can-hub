import { useEffect, useState } from 'react'
import { api, PERMISSION, setUnauthorizedHandler, type AuthState } from './api'
import { useAuth } from './hooks'
import { Acls } from './components/Acls'
import { Agents } from './components/Agents'
import { Audit } from './components/Audit'
import { Bootstrap, Login } from './components/Login'
import { ChangePassword } from './components/ChangePassword'
import { Clients } from './components/Clients'
import { Dashboard } from './components/Dashboard'
import { Interfaces } from './components/Interfaces'
import { Peers } from './components/Peers'
import { Pins } from './components/Pins'
import { Users } from './components/Users'
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
  useEffect(() => {
    // Any 401 from a request drops us back to the login screen at once.
    setUnauthorizedHandler(reload)
    return () => setUnauthorizedHandler(null)
  }, [reload])

  if (!state) return <p style={{ padding: '2rem' }}>Loading…</p>
  if (state.needsBootstrap) return <Bootstrap onDone={reload} />
  if (!state.authenticated) return <Login onDone={reload} />
  return <Console auth={state} onLogout={reload} />
}

function Console({ auth, onLogout }: { auth: AuthState; onLogout: () => void }) {
  const allowed = TABS.filter(([, , permission]) => auth.permissions.includes(permission))
  const [tab, setTab] = useState<Tab>(() => {
    const fromHash = window.location.hash.replace(/^#/, '')
    return allowed.find(([id]) => id === fromHash)?.[0] ?? allowed[0]?.[0] ?? 'dashboard'
  })
  const [changingPassword, setChangingPassword] = useState(false)

  useEffect(() => {
    window.location.hash = tab
  }, [tab])

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
            {auth.user}{' '}
            <button onClick={() => setChangingPassword((open) => !open)}>Change password</button>{' '}
            <button onClick={logout}>Sign out</button>
          </span>
        </div>
        {changingPassword && <ChangePassword onClose={() => setChangingPassword(false)} />}
        <nav>
          {allowed.map(([id, label]) => (
            <button key={id} className={id === tab ? 'active' : ''} onClick={() => setTab(id)}>
              {label}
            </button>
          ))}
        </nav>
      </header>
      <main>
        {allowed.length === 0 ? (
          <p>Your account has no permissions yet. Ask an administrator to add you to a group.</p>
        ) : (
          <>
            {tab === 'dashboard' && <Dashboard />}
            {tab === 'peers' && <Peers permissions={auth.permissions} />}
            {tab === 'agents' && <Agents permissions={auth.permissions} />}
            {tab === 'clients' && <Clients />}
            {tab === 'interfaces' && <Interfaces permissions={auth.permissions} />}
            {tab === 'pins' && <Pins />}
            {tab === 'acls' && <Acls />}
            {tab === 'users' && <Users />}
            {tab === 'audit' && <Audit />}
          </>
        )}
      </main>
    </div>
  )
}

export default App
