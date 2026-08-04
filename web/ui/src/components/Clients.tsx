import { useState } from 'react'
import { api, PERMISSION, type Client } from '../api'
import { usePolling } from '../hooks'
import { can, peerId } from '../lib'
import { Fingerprint } from './ui/fingerprint'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'
import { GrantDialog } from './GrantDialog'

export function Clients({ permissions }: { permissions: string[] }) {
  const clients = usePolling(api.clients)
  const peers = usePolling(api.peers)
  const allowAcl = can(permissions, PERMISSION.aclManage)
  const [granting, setGranting] = useState<{ client: Client; fingerprint: string } | null>(null)
  const subjectByPeer = new Map((peers.data ?? []).map((p) => [p.peerId, p.fingerprintHex]))

  if (clients.error) return <p className="text-sm text-red-600">{clients.error}</p>
  if (!clients.data) return <p className="text-gray-500">Loading…</p>
  if (clients.data.length === 0) return <p className="text-gray-500">None.</p>

  return (
    <>
    <Table>
      <Thead>
        <Tr className="hover:bg-transparent">
          <Th>Peer</Th><Th>Subject</Th><Th>Interface</Th><Th>Channel</Th>
          <Th className="text-right">Forwarded</Th><Th className="text-right">Dropped</Th>
          {allowAcl && <Th></Th>}
        </Tr>
      </Thead>
      <Tbody>
        {clients.data.map((c) => {
          const fingerprint = subjectByPeer.get(c.peerId)
          return (
            <Tr key={`${c.peerId}-${c.interfaceId}-${c.channel}`}>
              <Td className="font-mono">{peerId(c.peerId)}</Td>
              <Td><Fingerprint value={fingerprint} /></Td>
              <Td>{c.agentName ? `${c.agentName}/${c.interfaceName}` : '—'}</Td>
              <Td>{c.channel ?? <Badge variant="secondary">idle</Badge>}</Td>
              <Td className="text-right tabular-nums">{c.framesForwarded.toLocaleString()}</Td>
              <Td className="text-right tabular-nums">{c.framesDropped.toLocaleString()}</Td>
              {allowAcl && (
                <Td>
                  <div className="flex justify-end">
                    {c.agentName && (
                      <Button
                        variant="outline"
                        size="sm"
                        onClick={() => setGranting({ client: c, fingerprint: fingerprint || '*' })}
                      >
                        Grant
                      </Button>
                    )}
                  </div>
                </Td>
              )}
            </Tr>
          )
        })}
      </Tbody>
    </Table>

    {granting && (
      <GrantDialog
        agentName={granting.client.agentName}
        interfaceName={granting.client.interfaceName}
        fingerprintHex={granting.fingerprint}
        onClose={() => setGranting(null)}
      />
    )}
    </>
  )
}
