import { api, PERMISSION } from '../api'
import { useAction, usePolling } from '../hooks'
import { can, peerId, shortFp, transportOf } from '../lib'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

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

  if (agents.error) return <p className="text-sm text-red-600">{agents.error}</p>
  if (!agents.data) return <p className="text-gray-500">Loading…</p>
  if (agents.data.length === 0) return <p className="text-gray-500">None.</p>

  const showActions = allowKick || allowPin
  return (
    <div className="space-y-3">
      {action.error && <p className="text-sm text-red-600">{action.error}</p>}
      <Table>
        <Thead>
          <Tr className="hover:bg-transparent">
            <Th>Agent</Th><Th>Peer</Th><Th>Transport</Th><Th className="text-right">Interfaces</Th>
            <Th>Fingerprint</Th><Th>Pinned</Th>{showActions && <Th></Th>}
          </Tr>
        </Thead>
        <Tbody>
          {agents.data.map((a) => {
            const isPinned = !!a.fingerprintHex && pinned.has(a.fingerprintHex)
            return (
              <Tr key={a.peerId}>
                <Td className="font-medium text-gray-900">{a.agentName}</Td>
                <Td className="font-mono">{peerId(a.peerId)}</Td>
                <Td><Badge variant="outline">{transportOf(a.peerId)}</Badge></Td>
                <Td className="text-right tabular-nums">{a.interfaceCount}</Td>
                <Td className="font-mono text-xs">{a.fingerprintHex ? shortFp(a.fingerprintHex) : '—'}</Td>
                <Td>
                  {isPinned ? <Badge variant="success">pinned</Badge> : <Badge variant="secondary">unpinned</Badge>}
                </Td>
                {showActions && (
                  <Td>
                    <div className="flex justify-end gap-2">
                      {allowPin && !isPinned && a.fingerprintHex && (
                        <Button size="sm" onClick={() => action.run(() => api.pinAdd(a.agentName, a.fingerprintHex))}>
                          Pin
                        </Button>
                      )}
                      {allowKick && (
                        <Button
                          variant="outline"
                          size="sm"
                          onClick={() => {
                            if (window.confirm(`Kick agent ${a.agentName}?`)) action.run(() => api.kickAgent(a.agentName))
                          }}
                        >
                          Kick
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
    </div>
  )
}
