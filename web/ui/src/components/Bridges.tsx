import { useState } from 'react'
import { api, type Bridge, type BridgeEndpoint, type CanFilter, type Interface } from '../api'
import { useAction, usePolling } from '../hooks'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { ConfirmButton } from './ui/confirm'
import { Input } from './ui/input'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

const EXACT_MASK = 0x1fffffff
const FILTERS_MAX = 16

export function Bridges() {
  const { data, error, refresh } = usePolling(api.bridges)
  const interfaces = usePolling(api.interfaces)
  const action = useAction(refresh)
  const [source, setSource] = useState('')
  const [destination, setDestination] = useState('')
  const [ids, setIds] = useState('')

  const add = (event: React.FormEvent) => {
    event.preventDefault()
    const from = endpointOf(source)
    const to = endpointOf(destination)
    const filters = filtersOf(ids)
    if (!from || !to) return action.setError('Name both buses as agent/interface')
    if (typeof filters === 'string') return action.setError(filters)
    action.run(() => api.bridgeSet(from, to, filters), `Bridging ${source} into ${destination}`)
  }

  return (
    <div className="space-y-4">
      {error && <p className="text-sm text-red-600">{error}</p>}
      {action.error && <p className="text-sm text-red-600">{action.error}</p>}
      <p className="text-sm text-gray-600">
        Frames captured on the source bus are injected into the destination bus. Two routes in opposite directions join
        both buses; a bridged frame is never bridged again, so they do not loop. For telemetry, diagnostics and
        replication, not for control loops over the network.
      </p>
      <form className="flex flex-wrap items-center gap-2" onSubmit={add}>
        <Input
          className="w-48"
          list="bridge-interfaces"
          placeholder="source agent/iface"
          value={source}
          onChange={(e) => setSource(e.target.value)}
        />
        <span className="text-gray-500">→</span>
        <Input
          className="w-48"
          list="bridge-interfaces"
          placeholder="destination agent/iface"
          value={destination}
          onChange={(e) => setDestination(e.target.value)}
        />
        <Input
          className="w-64"
          placeholder="hex ids: 100 200:700 (empty = all)"
          value={ids}
          onChange={(e) => setIds(e.target.value)}
        />
        <InterfaceNames id="bridge-interfaces" interfaces={interfaces.data} />
        <Button type="submit" disabled={action.pending}>{action.pending ? 'Adding…' : 'Add route'}</Button>
      </form>

      <Table>
        <Thead>
          <Tr className="hover:bg-transparent">
            <Th>Source</Th><Th>Destination</Th><Th>Ids</Th><Th>State</Th><Th>Forwarded</Th><Th>Dropped</Th><Th></Th>
          </Tr>
        </Thead>
        <Tbody>
          {(data ?? []).map((bridge) => (
            <Tr key={`${nameOf(bridge.source)}-${nameOf(bridge.destination)}`}>
              <Td>{nameOf(bridge.source)}</Td>
              <Td>{nameOf(bridge.destination)}</Td>
              <Td className="font-mono text-xs">{textOf(bridge.filters)}</Td>
              <Td><StateBadge bridge={bridge} /></Td>
              <Td>{bridge.forwarded}</Td>
              <Td>{bridge.dropped}</Td>
              <Td>
                <div className="flex justify-end">
                  <ConfirmButton
                    variant="outline"
                    size="sm"
                    disabled={action.pending}
                    title="Delete this route?"
                    description={`${nameOf(bridge.source)} stops feeding ${nameOf(bridge.destination)}.`}
                    confirmLabel="Delete"
                    destructive
                    onConfirm={() => action.run(() => api.bridgeDelete(bridge.source, bridge.destination), 'Route deleted')}
                  >
                    Delete
                  </ConfirmButton>
                </div>
              </Td>
            </Tr>
          ))}
          {data && data.length === 0 && (
            <Tr className="hover:bg-transparent"><Td className="text-gray-500" colSpan={7}>None.</Td></Tr>
          )}
        </Tbody>
      </Table>
    </div>
  )
}

function StateBadge({ bridge }: { bridge: Bridge }) {
  if (bridge.sourceOnline && bridge.destinationOnline) return <Badge variant="success">active</Badge>
  const missing = bridge.sourceOnline ? 'destination' : 'source'
  return <Badge variant="warning">waiting for {missing}</Badge>
}

function InterfaceNames({ id, interfaces }: { id: string; interfaces: Interface[] | null }) {
  return (
    <datalist id={id}>
      {(interfaces ?? []).map((i) => (
        <option key={`${i.agentName}/${i.interfaceName}`} value={`${i.agentName}/${i.interfaceName}`} />
      ))}
    </datalist>
  )
}

function nameOf(endpoint: BridgeEndpoint): string {
  return `${endpoint.agentName}/${endpoint.interfaceName}`
}

function endpointOf(text: string): BridgeEndpoint | null {
  const [agentName, interfaceName, ...rest] = text.trim().split('/')
  if (!agentName || !interfaceName || rest.length > 0) return null
  return { agentName, interfaceName }
}

// "100 200:700" → exact id 0x100 and the ids 0x200 matches under mask 0x700.
function filtersOf(text: string): CanFilter[] | string {
  const words = text.trim().split(/[\s,]+/).filter(Boolean)
  if (words.length > FILTERS_MAX) return `At most ${FILTERS_MAX} ids`
  const filters: CanFilter[] = []
  for (const word of words) {
    const [id, mask, ...rest] = word.split(':')
    const canId = parseInt(id, 16)
    const canMask = mask === undefined ? EXACT_MASK : parseInt(mask, 16)
    if (rest.length > 0 || !/^[0-9a-f]+$/i.test(id) || Number.isNaN(canMask) || (mask !== undefined && !/^[0-9a-f]+$/i.test(mask))) {
      return `${word} is not an id or id:mask in hex`
    }
    filters.push({ canId, canMask })
  }
  return filters
}

function textOf(filters: CanFilter[]): string {
  if (filters.length === 0) return 'all'
  return filters
    .map((f) => (f.canMask === EXACT_MASK ? hex(f.canId) : `${hex(f.canId)}:${hex(f.canMask)}`))
    .join(' ')
}

function hex(value: number): string {
  return value.toString(16).toUpperCase()
}
