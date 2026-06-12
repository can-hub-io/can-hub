import { api, PERMISSION, type Peer } from '../api'
import { can, peerId, shortFp, transportOf } from '../lib'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { DataView, type Column } from './DataView'

export function Peers({ permissions }: { permissions: string[] }) {
  const allowKick = can(permissions, PERMISSION.peersKick)
  const columns: Column<Peer>[] = [
    { header: 'Peer', render: (r) => <span className="font-mono">{peerId(r.peerId)}</span> },
    { header: 'Name', render: (r) => r.agentName || '—' },
    { header: 'Role', render: (r) => r.role },
    { header: 'Transport', render: (r) => <Badge variant="outline">{transportOf(r.peerId)}</Badge> },
    { header: 'Fingerprint', render: (r) => <span className="font-mono text-xs">{r.fingerprintHex ? shortFp(r.fingerprintHex) : '—'}</span> },
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
              <Button
                variant="outline"
                size="sm"
                onClick={() => {
                  if (window.confirm(`Kick peer ${peerId(r.peerId)}?`)) run(() => api.kickPeer(r.peerId))
                }}
              >
                Kick
              </Button>
            )
          : undefined
      }
    />
  )
}
