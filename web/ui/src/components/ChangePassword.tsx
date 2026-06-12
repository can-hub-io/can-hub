import { useState } from 'react'
import { api } from '../api'

// Self-service password change shown from the topbar. Keeps the current session
// alive; the daemon drops the user's other sessions.
export function ChangePassword({ onClose }: { onClose: () => void }) {
  const [current, setCurrent] = useState('')
  const [next, setNext] = useState('')
  const [confirm, setConfirm] = useState('')
  const [error, setError] = useState<string | null>(null)
  const [done, setDone] = useState(false)

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    setError(null)
    if (next.length < 8) return setError('new password must be at least 8 characters')
    if (next !== confirm) return setError('new passwords do not match')
    try {
      await api.changeOwnPassword(current, next)
      setDone(true)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
    }
  }

  if (done) {
    return (
      <div className="form" style={{ padding: '0.5rem 0' }}>
        <span>Password changed. Other sessions were signed out.</span>
        <button onClick={onClose}>Close</button>
      </div>
    )
  }

  return (
    <form className="form" onSubmit={submit} style={{ padding: '0.5rem 0' }}>
      <input placeholder="current password" type="password" autoComplete="current-password" value={current} onChange={(e) => setCurrent(e.target.value)} />
      <input placeholder="new password" type="password" autoComplete="new-password" value={next} onChange={(e) => setNext(e.target.value)} />
      <input placeholder="confirm new password" type="password" autoComplete="new-password" value={confirm} onChange={(e) => setConfirm(e.target.value)} />
      <button type="submit">Update</button>
      <button type="button" onClick={onClose}>Cancel</button>
      {error && <span className="error">{error}</span>}
    </form>
  )
}
