import { useState } from 'react'
import { Cable } from 'lucide-react'
import { api, type AuthState } from '../api'
import { Button } from './ui/button'
import { Input } from './ui/input'

export function Bootstrap({ onDone }: { onDone: () => void }) {
  return (
    <AuthForm
      title="Create the first admin"
      subtitle="Set up the initial administrator account"
      submitLabel="Create admin"
      confirmPassword
      action={(name, password) => api.setup(name, password)}
      onDone={onDone}
    />
  )
}

export function Login({ onDone }: { onDone: () => void }) {
  return (
    <AuthForm
      title="can-hub admin"
      subtitle="Sign in to the hub control panel"
      submitLabel="Sign in"
      action={(name, password) => api.login(name, password)}
      onDone={onDone}
    />
  )
}

function AuthForm({ title, subtitle, submitLabel, action, onDone, confirmPassword }: {
  title: string
  subtitle: string
  submitLabel: string
  action: (name: string, password: string) => Promise<AuthState>
  onDone: () => void
  confirmPassword?: boolean
}) {
  const [name, setName] = useState('')
  const [password, setPassword] = useState('')
  const [confirm, setConfirm] = useState('')
  const [error, setError] = useState<string | null>(null)

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    setError(null)
    if (confirmPassword) {
      if (password.length < 8) return setError('password must be at least 8 characters')
      if (password !== confirm) return setError('passwords do not match')
    }
    try {
      await action(name, password)
      onDone()
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  return (
    <div className="flex min-h-screen items-center justify-center px-4">
      <form onSubmit={submit} className="w-full max-w-sm rounded-xl border border-gray-200 bg-white p-8 shadow-sm">
        <div className="mb-6 flex flex-col items-center gap-2 text-center">
          <div className="flex h-11 w-11 items-center justify-center rounded-xl bg-primary-600 text-white">
            <Cable size={22} />
          </div>
          <h1 className="text-lg font-semibold text-gray-900">{title}</h1>
          <p className="text-sm text-gray-500">{subtitle}</p>
        </div>
        <div className="flex flex-col gap-3">
          <Input placeholder="user" autoComplete="username" value={name} onChange={(e) => setName(e.target.value)} autoFocus />
          <Input
            placeholder="password"
            type="password"
            autoComplete={confirmPassword ? 'new-password' : 'current-password'}
            value={password}
            onChange={(e) => setPassword(e.target.value)}
          />
          {confirmPassword && (
            <Input
              placeholder="confirm password"
              type="password"
              autoComplete="new-password"
              value={confirm}
              onChange={(e) => setConfirm(e.target.value)}
            />
          )}
          {error && <p className="text-sm text-red-600">{error}</p>}
          <Button type="submit" className="mt-1 w-full">{submitLabel}</Button>
        </div>
      </form>
    </div>
  )
}
