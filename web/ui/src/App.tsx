import { useEffect, useState } from 'react'
import {
  Cable, ChevronRight, Cpu, Fingerprint, LayoutDashboard, Lock, Plug, ScrollText, Share2,
  Users as UsersIcon, type LucideIcon,
} from 'lucide-react'
import { api, PERMISSION, setUnauthorizedHandler, type AuthState } from './api'
import { useAuth } from './hooks'
import { Sidebar, type NavItem } from './components/layout/Sidebar'
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

type Tab = 'dashboard' | 'peers' | 'agents' | 'clients' | 'interfaces' | 'pins' | 'acls' | 'users' | 'audit'

const NAV: { id: Tab; label: string; icon: LucideIcon; permission: string }[] = [
  { id: 'dashboard', label: 'Dashboard', icon: LayoutDashboard, permission: PERMISSION.viewsRead },
  { id: 'peers', label: 'Peers', icon: Share2, permission: PERMISSION.viewsRead },
  { id: 'agents', label: 'Agents', icon: Cpu, permission: PERMISSION.viewsRead },
  { id: 'clients', label: 'Clients', icon: Plug, permission: PERMISSION.viewsRead },
  { id: 'interfaces', label: 'Interfaces', icon: Cable, permission: PERMISSION.viewsRead },
  { id: 'pins', label: 'Pins', icon: Fingerprint, permission: PERMISSION.pinsManage },
  { id: 'acls', label: 'ACLs', icon: Lock, permission: PERMISSION.aclManage },
  { id: 'users', label: 'Users', icon: UsersIcon, permission: PERMISSION.usersManage },
  { id: 'audit', label: 'Audit', icon: ScrollText, permission: PERMISSION.usersManage },
]

function App() {
  const { state, reload } = useAuth()
  useEffect(() => {
    setUnauthorizedHandler(reload)
    return () => setUnauthorizedHandler(null)
  }, [reload])

  if (!state) return <p className="p-8 text-gray-500">Loading…</p>
  if (state.needsBootstrap) return <Bootstrap onDone={reload} />
  if (!state.authenticated) return <Login onDone={reload} />
  return <Console auth={state} onLogout={reload} />
}

function Console({ auth, onLogout }: { auth: AuthState; onLogout: () => void }) {
  const allowed = NAV.filter((item) => auth.permissions.includes(item.permission))
  const [tab, setTab] = useState<Tab>(() => {
    const fromHash = window.location.hash.replace(/^#/, '') as Tab
    return allowed.find((item) => item.id === fromHash)?.id ?? allowed[0]?.id ?? 'dashboard'
  })
  const [changingPassword, setChangingPassword] = useState(false)

  useEffect(() => {
    window.location.hash = tab
  }, [tab])

  const logout = async () => {
    await api.logout()
    onLogout()
  }

  const navItems: NavItem[] = allowed.map(({ id, label, icon }) => ({ id, label, icon }))
  const current = allowed.find((item) => item.id === tab)

  return (
    <div className="flex h-screen bg-gray-50">
      <Sidebar
        items={navItems}
        active={tab}
        onSelect={(id) => setTab(id as Tab)}
        user={auth.user}
        onChangePassword={() => setChangingPassword(true)}
        onLogout={logout}
      />
      <main className="flex-1 overflow-y-auto">
        <div className="sticky top-0 z-30 border-b border-gray-200 bg-gray-50 px-8 pb-4 pt-3">
          <div className="mb-1 flex items-center gap-1 text-xs text-gray-400">
            <span>can-hub</span>
            <ChevronRight size={12} />
            <span className="text-gray-600">{current?.label ?? 'Admin'}</span>
          </div>
          <h1 className="text-xl font-semibold text-gray-900">{current?.label ?? 'can-hub'}</h1>
        </div>
        <div className="max-w-5xl px-8 py-6">
          {allowed.length === 0 ? (
            <p className="text-gray-500">Your account has no permissions yet. Ask an administrator to add you to a group.</p>
          ) : (
            <>
              {tab === 'dashboard' && <Dashboard />}
              {tab === 'peers' && <Peers permissions={auth.permissions} />}
              {tab === 'agents' && <Agents permissions={auth.permissions} />}
              {tab === 'clients' && <Clients />}
              {tab === 'interfaces' && <Interfaces permissions={auth.permissions} />}
              {tab === 'pins' && <Pins />}
              {tab === 'acls' && <Acls />}
              {tab === 'users' && <Users currentUser={auth.user} />}
              {tab === 'audit' && <Audit />}
            </>
          )}
        </div>
      </main>
      {changingPassword && <ChangePassword onClose={() => setChangingPassword(false)} />}
    </div>
  )
}

export default App
