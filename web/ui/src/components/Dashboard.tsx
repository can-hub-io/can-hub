import { api } from '../api'
import { usePolling, useTelemetry } from '../hooks'
import { Card } from './ui/card'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <Card className="p-4">
      <div className="text-2xl font-semibold tabular-nums text-gray-900">{value}</div>
      <div className="mt-0.5 text-sm text-gray-500">{label}</div>
    </Card>
  )
}

export function Dashboard() {
  const { frame, connected } = useTelemetry()
  const { data: status, error } = usePolling(api.status)

  const counts: [string, string][] = [
    ['Peers', status ? String(status.peerCount) : '—'],
    ['Agents', status ? String(status.agentCount) : '—'],
    ['Clients', status ? String(status.clientCount) : '—'],
    ['Interfaces', status ? String(status.interfaceCount) : '—'],
  ]
  const rates: [string, number | undefined][] = [
    ['Received /s', frame?.rates.receivedPerS],
    ['Forwarded /s', frame?.rates.forwardedPerS],
    ['Dropped /s', frame?.rates.droppedPerS],
    ['Unroutable /s', frame?.rates.unroutablePerS],
  ]

  return (
    <div className="space-y-6">
      {error && <p className="text-sm text-red-600">{error}</p>}

      <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
        {counts.map(([label, value]) => <Stat key={label} label={label} value={value} />)}
      </div>

      <div>
        <h2 className="mb-2 flex items-center gap-2 text-base font-semibold text-gray-900">
          Live rates
          <span
            className={`inline-block h-2 w-2 rounded-full ${connected ? 'bg-green-500' : 'bg-gray-300'}`}
            title={connected ? 'connected' : 'offline'}
          />
        </h2>
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
          {rates.map(([label, value]) => (
            <Stat key={label} label={label} value={value === undefined ? '—' : value.toFixed(1)} />
          ))}
        </div>
      </div>

      <div>
        <h2 className="mb-2 text-base font-semibold text-gray-900">Per-interface throughput</h2>
        <Table>
          <Thead>
            <Tr className="hover:bg-transparent">
              <Th>Interface</Th>
              <Th className="text-right">Frames</Th>
              <Th className="text-right">Frames /s</Th>
            </Tr>
          </Thead>
          <Tbody>
            {(frame?.interfaces ?? []).map((row) => (
              <Tr key={row.interfaceId}>
                <Td className="font-mono text-gray-800">{row.agentName}/{row.interfaceName}</Td>
                <Td className="text-right tabular-nums">{row.framesReceived.toLocaleString()}</Td>
                <Td className="text-right tabular-nums">{row.framesPerS.toFixed(1)}</Td>
              </Tr>
            ))}
            {!frame && (
              <Tr className="hover:bg-transparent">
                <Td className="text-gray-500" colSpan={3}>Waiting for telemetry…</Td>
              </Tr>
            )}
          </Tbody>
        </Table>
      </div>
    </div>
  )
}
