import { useState } from 'react'
import { api, type AuthState } from '../api'

export function Bootstrap({ onDone }: { onDone: () => void }) {
  return (
    <AuthForm
      title="Create the first admin"
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
      submitLabel="Sign in"
      action={(name, password) => api.login(name, password)}
      onDone={onDone}
    />
  )
}

function AuthForm({ title, submitLabel, action, onDone, confirmPassword }: {
  title: string
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
    <div className="app" style={{ maxWidth: 360 }}>
      <h1>{title}</h1>
      <form className="form" onSubmit={submit} style={{ flexDirection: 'column', alignItems: 'stretch' }}>
        <input placeholder="user" autoComplete="username" value={name} onChange={(e) => setName(e.target.value)} autoFocus />
        <input
          placeholder="password"
          type="password"
          autoComplete={confirmPassword ? 'new-password' : 'current-password'}
          value={password}
          onChange={(e) => setPassword(e.target.value)}
        />
        {confirmPassword && (
          <input
            placeholder="confirm password"
            type="password"
            autoComplete="new-password"
            value={confirm}
            onChange={(e) => setConfirm(e.target.value)}
          />
        )}
        {error && <p className="error">{error}</p>}
        <button type="submit">{submitLabel}</button>
      </form>
    </div>
  )
}
