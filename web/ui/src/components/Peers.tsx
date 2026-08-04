import { api, PERMISSION, type Peer } from '../api'
import { can, formatUptime, peerId } from '../lib'
import { Fingerprint } from './ui/fingerprint'
import { Badge } from './ui/badge'
import { ConfirmButton } from './ui/confirm'
import { DataView, type Column } from './DataView'

export function Peers({ permissions }: { permissions: string[] }) {
  const allowKick = can(permissions, PERMISSION.peersKick)
  const columns: Column<Peer>[] = [
    { header: 'Peer', render: (r) => <span className="font-mono">{peerId(r.peerId)}</span> },
    { header: 'Name', render: (r) => r.agentName || '—' },
    { header: 'Role', render: (r) => r.role },
    { header: 'Transport', render: (r) => <Badge variant="outline">{r.transport}</Badge> },
    { header: 'Origin', render: (r) => <span className="font-mono text-xs">{r.origin || '—'}</span> },
    { header: 'Uptime', render: (r) => formatUptime(r.uptimeSeconds), num: true },
    { header: 'Fingerprint', render: (r) => <Fingerprint value={r.fingerprintHex} /> },
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
          ? (r, run, pending) => (
              <ConfirmButton
                variant="outline"
                size="sm"
                disabled={pending}
                title={`Kick peer ${peerId(r.peerId)}?`}
                description="The connection is dropped immediately; the peer may reconnect."
                confirmLabel="Kick"
                destructive
                onConfirm={() => run(() => api.kickPeer(r.peerId), `Peer ${peerId(r.peerId)} kicked`)}
              >
                Kick
              </ConfirmButton>
            )
          : undefined
      }
    />
  )
}
