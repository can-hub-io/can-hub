import { api } from '../api'
import { usePolling } from '../hooks'
import { peerId, shortFp } from '../lib'

export function Clients() {
  const clients = usePolling(api.clients)
  const peers = usePolling(api.peers)
  // The client view carries no fingerprint; join the peer view by peerId so a
  // channel can be matched to its ACL subject.
  const subjectByPeer = new Map((peers.data ?? []).map((p) => [p.peerId, p.fingerprintHex]))

  if (clients.error) return <p className="error">{clients.error}</p>
  if (!clients.data) return <p>Loading…</p>
  if (clients.data.length === 0) return <p>None.</p>

  return (
    <section>
      <table>
        <thead>
          <tr>
            <th>Peer</th><th>Subject</th><th>Interface</th><th>Channel</th>
            <th className="num">Forwarded</th><th className="num">Dropped</th>
          </tr>
        </thead>
        <tbody>
          {clients.data.map((c) => {
            const fingerprint = subjectByPeer.get(c.peerId)
            return (
              <tr key={`${c.peerId}-${c.interfaceId}-${c.channel}`}>
                <td>{peerId(c.peerId)}</td>
                <td>{fingerprint ? shortFp(fingerprint) : '—'}</td>
                <td>{c.agentName ? `${c.agentName}/${c.interfaceName}` : '—'}</td>
                <td>{c.channel ?? 'idle'}</td>
                <td className="num">{c.framesForwarded.toLocaleString()}</td>
                <td className="num">{c.framesDropped.toLocaleString()}</td>
              </tr>
            )
          })}
        </tbody>
      </table>
    </section>
  )
}
