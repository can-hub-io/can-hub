import { useState } from 'react'
import { api, PERMISSION, type Interface } from '../api'
import { usePolling } from '../hooks'
import { can } from '../lib'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { ConfigureDialog } from './ConfigureDialog'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'
import { GrantDialog } from './GrantDialog'

// A bridge group is listed as group/<name>; the hub serves it, nothing to configure.
const GROUP_AGENT = 'group'

export function Interfaces({ permissions }: { permissions: string[] }) {
  const { data, error } = usePolling(api.interfaces)
  const allowConfig = can(permissions, PERMISSION.interfacesConfig)
  const allowAcl = can(permissions, PERMISSION.aclManage)
  const [granting, setGranting] = useState<Interface | null>(null)
  const [configuring, setConfiguring] = useState<Interface | null>(null)
  const hasActions = allowConfig || allowAcl

  return (
    <div className="space-y-4">
      {error && <p className="text-sm text-red-600">{error}</p>}
      <Table>
        <Thead>
          <Tr className="hover:bg-transparent">
            <Th>Interface</Th><Th>Mode</Th><Th className="text-right">Subscribers</Th><Th className="text-right">Frames</Th><Th className="text-right">TX dropped</Th>
            {hasActions && <Th></Th>}
          </Tr>
        </Thead>
        <Tbody>
          {(data ?? []).map((i) => (
            <Tr key={i.interfaceId}>
              <Td className="font-mono text-gray-800">{i.agentName}/{i.interfaceName}</Td>
              <Td>{i.mode === 'unknown' ? <span className="text-gray-400">—</span> : <Badge variant="secondary">{i.mode === 'fd' ? 'CAN FD' : 'classic'}</Badge>}</Td>
              <Td className="text-right tabular-nums">{i.subscriberCount}</Td>
              <Td className="text-right tabular-nums">{i.framesReceived.toLocaleString()}</Td>
              <Td className="text-right tabular-nums">{i.txDropped.toLocaleString()}</Td>
              {hasActions && (
                <Td>
                  <div className="flex justify-end gap-2">
                    {allowConfig && i.agentName !== GROUP_AGENT && (
                      <Button variant="outline" size="sm" onClick={() => setConfiguring(i)}>Configure…</Button>
                    )}
                    {allowAcl && <Button variant="outline" size="sm" onClick={() => setGranting(i)}>Grant</Button>}
                  </div>
                </Td>
              )}
            </Tr>
          ))}
          {data && data.length === 0 && (
            <Tr className="hover:bg-transparent"><Td className="text-gray-500" colSpan={hasActions ? 6 : 5}>None.</Td></Tr>
          )}
        </Tbody>
      </Table>

      {configuring && (
        <ConfigureDialog
          agentName={configuring.agentName}
          interfaceName={configuring.interfaceName}
          onClose={() => setConfiguring(null)}
        />
      )}
      {granting && (
        <GrantDialog
          agentName={granting.agentName}
          interfaceName={granting.interfaceName}
          onClose={() => setGranting(null)}
        />
      )}
    </div>
  )
}
