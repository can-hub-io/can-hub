import { api } from '../api'
import { usePolling } from '../hooks'
import { peerId, shortFp } from '../lib'
import { Badge } from './ui/badge'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

export function Clients() {
  const clients = usePolling(api.clients)
  const peers = usePolling(api.peers)
  const subjectByPeer = new Map((peers.data ?? []).map((p) => [p.peerId, p.fingerprintHex]))

  if (clients.error) return <p className="text-sm text-red-600">{clients.error}</p>
  if (!clients.data) return <p className="text-gray-500">Loading…</p>
  if (clients.data.length === 0) return <p className="text-gray-500">None.</p>

  return (
    <Table>
      <Thead>
        <Tr className="hover:bg-transparent">
          <Th>Peer</Th><Th>Subject</Th><Th>Interface</Th><Th>Channel</Th>
          <Th className="text-right">Forwarded</Th><Th className="text-right">Dropped</Th>
        </Tr>
      </Thead>
      <Tbody>
        {clients.data.map((c) => {
          const fingerprint = subjectByPeer.get(c.peerId)
          return (
            <Tr key={`${c.peerId}-${c.interfaceId}-${c.channel}`}>
              <Td className="font-mono">{peerId(c.peerId)}</Td>
              <Td className="font-mono text-xs">{fingerprint ? shortFp(fingerprint) : '—'}</Td>
              <Td>{c.agentName ? `${c.agentName}/${c.interfaceName}` : '—'}</Td>
              <Td>{c.channel ?? <Badge variant="secondary">idle</Badge>}</Td>
              <Td className="text-right tabular-nums">{c.framesForwarded.toLocaleString()}</Td>
              <Td className="text-right tabular-nums">{c.framesDropped.toLocaleString()}</Td>
            </Tr>
          )
        })}
      </Tbody>
    </Table>
  )
}
