import { useState } from 'react'
import { api } from '../api'
import { useAction, usePolling } from '../hooks'
import { shortFp } from '../lib'

export function Pins() {
  const { data, error, refresh } = usePolling(api.pins)
  const action = useAction(refresh)
  const [name, setName] = useState('')
  const [fingerprint, setFingerprint] = useState('')

  const add = () => {
    if (!name || !fingerprint) {
      action.setError('name and fingerprint required')
      return
    }
    action.run(async () => {
      await api.pinAdd(name, fingerprint)
      setName('')
      setFingerprint('')
    })
  }

  return (
    <section>
      {error && <p className="error">{error}</p>}
      {action.error && <p className="error">{action.error}</p>}
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
                <button
                  onClick={() => {
                    if (window.confirm(`Delete pin for ${p.agentName}?`)) action.run(() => api.pinDelete(p.agentName))
                  }}
                >
                  Delete
                </button>
              </td>
            </tr>
          ))}
          {data && data.length === 0 && <tr><td colSpan={3}>None.</td></tr>}
        </tbody>
      </table>
    </section>
  )
}
