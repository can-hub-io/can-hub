import { api, PERMISSION, type Peer } from '../api'
import { can, peerId, shortFp, transportOf } from '../lib'
import { DataView, type Column } from './DataView'

export function Peers({ permissions }: { permissions: string[] }) {
  const allowKick = can(permissions, PERMISSION.peersKick)
  const columns: Column<Peer>[] = [
    { header: 'Peer', render: (r) => peerId(r.peerId) },
    { header: 'Transport', render: (r) => transportOf(r.peerId) },
    { header: 'Role', render: (r) => r.role },
    { header: 'Agent', render: (r) => r.agentName || '—' },
    { header: 'Fingerprint', render: (r) => (r.fingerprintHex ? shortFp(r.fingerprintHex) : '—') },
    { header: 'Forwarded', render: (r) => r.framesForwarded.toLocaleString(), num: true },
    { header: 'Dropped', render: (r) => r.framesDropped.toLocaleString(), num: true },
  ]
  return (
    <DataView<Peer>
      fetcher={api.peers}
      rowKey={(r) => r.peerId}
      columns={columns}
      actions={
        allowKick
          ? (r, run) => (
              <button
                onClick={() => {
                  if (window.confirm(`Kick peer ${peerId(r.peerId)}?`)) run(() => api.kickPeer(r.peerId))
                }}
              >
                Kick
              </button>
            )
          : undefined
      }
    />
  )
}
