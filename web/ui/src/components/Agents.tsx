import { api, PERMISSION } from '../api'
import { useAction, usePolling } from '../hooks'
import { can, peerId, shortFp, transportOf } from '../lib'

export function Agents({ permissions }: { permissions: string[] }) {
  const agents = usePolling(api.agents)
  const pins = usePolling(api.pins)
  const action = useAction(() => {
    agents.refresh()
    pins.refresh()
  })
  const allowKick = can(permissions, PERMISSION.peersKick)
  const allowPin = can(permissions, PERMISSION.pinsManage)
  const pinned = new Set((pins.data ?? []).map((p) => p.fingerprintHex))

  if (agents.error) return <p className="error">{agents.error}</p>
  if (!agents.data) return <p>Loading…</p>
  if (agents.data.length === 0) return <p>None.</p>

  const showActions = allowKick || allowPin
  return (
    <section>
      {action.error && <p className="error">{action.error}</p>}
      <table>
        <thead>
          <tr>
            <th>Agent</th><th>Peer</th><th>Transport</th><th className="num">Interfaces</th>
            <th>Fingerprint</th><th>Pinned</th>{showActions && <th></th>}
          </tr>
        </thead>
        <tbody>
          {agents.data.map((a) => {
            const isPinned = !!a.fingerprintHex && pinned.has(a.fingerprintHex)
            return (
              <tr key={a.peerId}>
                <td>{a.agentName}</td>
                <td>{peerId(a.peerId)}</td>
                <td>{transportOf(a.peerId)}</td>
                <td className="num">{a.interfaceCount}</td>
                <td>{a.fingerprintHex ? shortFp(a.fingerprintHex) : '—'}</td>
                <td>
                  <span className={isPinned ? 'dot live' : 'dot'} title={isPinned ? 'pinned' : 'unpinned'} />{' '}
                  {isPinned ? 'pinned' : 'unpinned'}
                </td>
                {showActions && (
                  <td className="num">
                    {allowPin && !isPinned && a.fingerprintHex && (
                      <button onClick={() => action.run(() => api.pinAdd(a.agentName, a.fingerprintHex))}>Pin</button>
                    )}{' '}
                    {allowKick && (
                      <button
                        onClick={() => {
                          if (window.confirm(`Kick agent ${a.agentName}?`)) action.run(() => api.kickAgent(a.agentName))
                        }}
                      >
                        Kick
                      </button>
                    )}
                  </td>
                )}
              </tr>
            )
          })}
        </tbody>
      </table>
    </section>
  )
}
