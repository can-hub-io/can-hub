import { type LucideIcon, KeyRound, LogOut } from 'lucide-react'
import { cn } from '../../lib/utils'
import { Logo } from '../ui/Logo'

export interface NavItem {
  id: string
  label: string
  icon: LucideIcon
}

export function Sidebar({ items, active, onSelect, user, onChangePassword, onLogout, open, onClose }: {
  items: NavItem[]
  active: string
  onSelect: (id: string) => void
  user: string | null
  onChangePassword: () => void
  onLogout: () => void
  open: boolean
  onClose: () => void
}) {
  const initials = (user ?? '?').slice(0, 2).toUpperCase()
  return (
    <>
      {open && (
        <div
          className="fixed inset-0 z-40 bg-black/40 md:hidden"
          aria-hidden="true"
          onClick={onClose}
        />
      )}
      <aside
        className={cn(
          'fixed inset-y-0 left-0 z-50 flex h-screen w-56 shrink-0 flex-col border-r border-gray-200 bg-white transition-transform',
          'md:static md:translate-x-0',
          open ? 'translate-x-0' : '-translate-x-full',
        )}
      >
      <div className="flex items-center gap-2 border-b border-gray-100 px-5 py-4">
        <Logo size={28} />
        <div className="font-semibold tracking-tight text-gray-900">can-hub</div>
      </div>

      <nav className="flex-1 space-y-0.5 overflow-y-auto px-3 py-4">
        {items.map(({ id, label, icon: Icon }) => (
          <button
            key={id}
            onClick={() => {
              onSelect(id)
              onClose()
            }}
            className={cn(
              'flex w-full items-center gap-3 rounded-md px-3 py-2 text-sm font-medium transition-colors',
              id === active
                ? 'bg-primary-50 text-primary-700'
                : 'text-gray-600 hover:bg-gray-100 hover:text-gray-900',
            )}
          >
            <Icon size={16} />
            {label}
          </button>
        ))}
      </nav>

      <div className="border-t border-gray-100 p-3">
        <div className="flex items-center gap-2 px-1 py-1.5">
          <div className="flex h-8 w-8 items-center justify-center rounded-full bg-primary-100 text-xs font-bold text-primary-700">
            {initials}
          </div>
          <span className="truncate text-sm font-medium text-gray-800">{user}</span>
        </div>
        <div className="mt-1 flex flex-col gap-0.5">
          <button
            onClick={() => {
              onChangePassword()
              onClose()
            }}
            className="flex items-center gap-2 rounded-md px-2 py-1.5 text-sm text-gray-600 hover:bg-gray-100 hover:text-gray-900"
          >
            <KeyRound size={15} /> Change password
          </button>
          <button
            onClick={onLogout}
            className="flex items-center gap-2 rounded-md px-2 py-1.5 text-sm text-gray-600 hover:bg-gray-100 hover:text-gray-900"
          >
            <LogOut size={15} /> Sign out
          </button>
        </div>
      </div>
      </aside>
    </>
  )
}
