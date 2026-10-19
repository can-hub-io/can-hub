import { useState } from 'react'
import { api, PERMISSION, type Rejected } from '../api'
import { useAction, usePolling } from '../hooks'

type Run = ReturnType<typeof useAction>['run']
import { can, formatUptime } from '../lib'
import { Fingerprint } from './ui/fingerprint'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { ConfirmButton } from './ui/confirm'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'
import { GrantDialog } from './GrantDialog'

export function Pending({ permissions }: { permissions: string[] }) {
  const { data, error, refresh } = usePolling(api.rejected)
  const action = useAction(refresh)
  const allowAcl = can(permissions, PERMISSION.aclManage)
  const [granting, setGranting] = useState<Rejected | null>(null)

  return (
    <div className="space-y-4">
      {error && <p className="text-sm text-red-600">{error}</p>}
      {action.error && <p className="text-sm text-red-600">{action.error}</p>}
      <p className="text-xs text-gray-500">
        Identities the hub turned away: agents and clients it does not know while it runs with
        --require-known-peers, and agents whose name is pinned to another fingerprint. An identity
        leaves the list once it connects.
      </p>

      <Table>
        <Thead>
          <Tr className="hover:bg-transparent">
            <Th>Role</Th><Th>Name</Th><Th>Fingerprint</Th><Th>Origin</Th><Th>Reason</Th>
            <Th className="text-right">Attempts</Th><Th>Last seen</Th><Th></Th>
          </Tr>
        </Thead>
        <Tbody>
          {(data ?? []).map((r) => (
            <Tr key={`${r.role}-${r.fingerprintHex}`}>
              <Td><Badge variant="secondary">{r.role}</Badge></Td>
              <Td className="font-medium text-gray-900">{r.name || '—'}</Td>
              <Td><Fingerprint value={r.fingerprintHex} /></Td>
              <Td className="font-mono">{r.origin || '—'}</Td>
              <Td>
                {r.reason === 'fingerprint-mismatch'
                  ? <Badge variant="warning">fingerprint mismatch</Badge>
                  : <Badge variant="secondary">not allowed</Badge>}
              </Td>
              <Td className="text-right">{r.attempts}</Td>
              <Td>{formatUptime(r.lastSeenSeconds)} ago</Td>
              <Td>
                <div className="flex justify-end">
                  <RejectedAction rejected={r} allowAcl={allowAcl} pending={action.pending} run={action.run} onGrant={setGranting} />
                </div>
              </Td>
            </Tr>
          ))}
          {data && data.length === 0 && (
            <Tr className="hover:bg-transparent"><Td className="text-gray-500" colSpan={8}>None.</Td></Tr>
          )}
        </Tbody>
      </Table>

      {granting && (
        <GrantDialog fingerprintHex={granting.fingerprintHex} onClose={() => { setGranting(null); refresh() }} />
      )}
    </div>
  )
}

function RejectedAction({ rejected, allowAcl, pending, run, onGrant }: {
  rejected: Rejected
  allowAcl: boolean
  pending: boolean
  run: Run
  onGrant: (rejected: Rejected) => void
}) {
  if (rejected.role === 'client') {
    if (!allowAcl) return null
    return <Button variant="outline" size="sm" disabled={pending} onClick={() => onGrant(rejected)}>Grant…</Button>
  }

  if (rejected.reason === 'fingerprint-mismatch') {
    return (
      <ConfirmButton
        variant="outline"
        size="sm"
        disabled={pending}
        title={`Replace the pin of ${rejected.name}?`}
        description="The hub knows this agent under another fingerprint. Replace it only if you re-keyed the device yourself: an impostor claiming the name looks exactly like this."
        confirmLabel="Replace pin"
        destructive
        onConfirm={() => run(async () => {
          await api.pinDelete(rejected.name)
          await api.pinAdd(rejected.name, rejected.fingerprintHex)
        }, `Pin for ${rejected.name} replaced`)}
      >
        Replace pin
      </ConfirmButton>
    )
  }

  return (
    <ConfirmButton
      variant="outline"
      size="sm"
      disabled={pending}
      title={`Authorize ${rejected.name}?`}
      description="Pins this fingerprint to the agent name. The agent gets in on its next retry."
      confirmLabel="Authorize"
      onConfirm={() => run(() => api.pinAdd(rejected.name, rejected.fingerprintHex), `Authorized ${rejected.name}`)}
    >
      Authorize
    </ConfirmButton>
  )
}
