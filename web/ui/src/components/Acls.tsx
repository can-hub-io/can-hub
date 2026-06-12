import { useState } from 'react'
import { api, type AclLevel } from '../api'
import { useAction, usePolling } from '../hooks'
import { shortFp } from '../lib'

export function Acls() {
  const { data, error, refresh } = usePolling(api.acls)
  const action = useAction(refresh)
  const [fingerprint, setFingerprint] = useState('*')
  const [agent, setAgent] = useState('*')
  const [iface, setIface] = useState('*')
  const [level, setLevel] = useState<AclLevel>('ro')

  const grant = () => {
    action.run(() => api.aclSet(fingerprint || '*', agent || '*', iface || '*', level))
  }

  return (
    <section>
      {error && <p className="error">{error}</p>}
      {action.error && <p className="error">{action.error}</p>}
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
                <button
                  onClick={() => {
                    if (window.confirm(`Revoke grant for ${shortFp(a.fingerprintHex)} on ${a.agentName}/${a.interfaceName}?`))
                      action.run(() => api.aclRevoke(a.fingerprintHex, a.agentName, a.interfaceName))
                  }}
                >
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
