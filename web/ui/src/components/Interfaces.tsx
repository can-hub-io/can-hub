import { useState } from 'react'
import { api, PERMISSION, type IfconfigOp } from '../api'
import { useAction, usePolling } from '../hooks'
import { can } from '../lib'
import { Button } from './ui/button'
import { Input } from './ui/input'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

const selectClass =
  'h-9 rounded-md border border-gray-300 bg-white px-3 text-sm text-gray-900 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary-500/40'

export function Interfaces({ permissions }: { permissions: string[] }) {
  const { data, error, refresh } = usePolling(api.interfaces)
  const action = useAction(refresh)
  const allowConfig = can(permissions, PERMISSION.interfacesConfig)
  const [selected, setSelected] = useState('')
  const [op, setOp] = useState<IfconfigOp>('bitrate')
  const [bitrate, setBitrate] = useState(500000)

  const apply = () => {
    const iface = (data ?? []).find((i) => `${i.agentName}/${i.interfaceName}` === selected)
    if (!iface) return action.setError('select an interface')
    action.run(() => api.interfaceConfig(iface.agentName, iface.interfaceName, op, bitrate))
  }

  return (
    <div className="space-y-4">
      {error && <p className="text-sm text-red-600">{error}</p>}
      {action.error && <p className="text-sm text-red-600">{action.error}</p>}
      {allowConfig && (
        <div className="flex flex-wrap items-center gap-2">
          <select className={selectClass} value={selected} onChange={(e) => setSelected(e.target.value)}>
            <option value="">interface…</option>
            {(data ?? []).map((i) => {
              const name = `${i.agentName}/${i.interfaceName}`
              return <option key={i.interfaceId} value={name}>{name}</option>
            })}
          </select>
          <select className={selectClass} value={op} onChange={(e) => setOp(e.target.value as IfconfigOp)}>
            <option value="bitrate">set bitrate</option>
            <option value="up">link up</option>
            <option value="down">link down</option>
          </select>
          {op === 'bitrate' && (
            <Input type="number" className="w-32" value={bitrate} onChange={(e) => setBitrate(Number(e.target.value))} />
          )}
          <Button onClick={apply}>Apply</Button>
        </div>
      )}

      <Table>
        <Thead>
          <Tr className="hover:bg-transparent">
            <Th>Interface</Th><Th className="text-right">Subscribers</Th><Th className="text-right">Frames</Th><Th className="text-right">TX dropped</Th>
          </Tr>
        </Thead>
        <Tbody>
          {(data ?? []).map((i) => (
            <Tr key={i.interfaceId}>
              <Td className="font-mono text-gray-800">{i.agentName}/{i.interfaceName}</Td>
              <Td className="text-right tabular-nums">{i.subscriberCount}</Td>
              <Td className="text-right tabular-nums">{i.framesReceived.toLocaleString()}</Td>
              <Td className="text-right tabular-nums">{i.txDropped.toLocaleString()}</Td>
            </Tr>
          ))}
          {data && data.length === 0 && (
            <Tr className="hover:bg-transparent"><Td className="text-gray-500" colSpan={4}>None.</Td></Tr>
          )}
        </Tbody>
      </Table>
    </div>
  )
}
