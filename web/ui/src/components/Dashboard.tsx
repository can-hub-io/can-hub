import { api, type TelemetryFrame, type TransactionRow } from '../api'
import { usePolling, useTelemetry } from '../hooks'
import { formatBytes, formatUptime, peerId } from '../lib'
import { Badge } from './ui/badge'
import { Card } from './ui/card'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

function LiveDot({ connected }: { connected: boolean }) {
  return (
    <span
      className={`inline-block h-2 w-2 rounded-full ${connected ? 'bg-green-500' : 'bg-gray-300'}`}
      title={connected ? 'connected' : 'offline'}
    />
  )
}

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
          <LiveDot connected={connected} />
        </h2>
        <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
          {rates.map(([label, value]) => (
            <Stat key={label} label={label} value={value === undefined ? '—' : value.toFixed(1)} />
          ))}
        </div>
      </div>

      <Transactions frame={frame} connected={connected} />

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

function Transactions({ frame, connected }: { frame: TelemetryFrame | null; connected: boolean }) {
  const kpis = frame?.transactions
  const stats: [string, string][] = [
    ['Active', kpis ? String(kpis.active) : '—'],
    ['Steps /s', kpis ? kpis.stepsPerS.toFixed(1) : '—'],
    ['Success', kpis?.successRatio == null ? '—' : `${(kpis.successRatio * 100).toFixed(1)} %`],
    ['Lease drops', kpis ? kpis.leaseDrops.toLocaleString() : '—'],
  ]
  const rows = frame?.transactionsInFlight ?? []

  return (
    <div>
      <h2 className="mb-2 flex items-center gap-2 text-base font-semibold text-gray-900">
        Transactions
        <LiveDot connected={connected} />
      </h2>
      <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
        {stats.map(([label, value]) => <Stat key={label} label={label} value={value} />)}
      </div>

      <h3 className="mb-2 mt-4 text-sm font-semibold text-gray-700">In flight</h3>
      <Table>
        <Thead>
          <Tr className="hover:bg-transparent">
            <Th>Interface</Th>
            <Th>Client</Th>
            <Th>State</Th>
            <Th>Lease</Th>
            <Th className="text-right">Steps</Th>
            <Th className="text-right">In flight</Th>
            <Th className="text-right">Age</Th>
            <Th className="text-right">Last progress</Th>
          </Tr>
        </Thead>
        <Tbody>
          {rows.map((row) => (
            <Tr key={`${row.interface}-${row.clientPeerId}`}>
              <Td className="font-mono text-gray-800">{row.interface}</Td>
              <Td className="font-mono text-gray-800">{clientOf(row)}</Td>
              <Td><Badge variant={row.state === 'active' ? 'success' : 'warning'}>{row.state}</Badge></Td>
              <Td>{row.exclusive ? 'yes' : 'no'}</Td>
              <Td className="text-right tabular-nums">{row.completedSteps.toLocaleString()}</Td>
              <Td className="text-right tabular-nums">{formatBytes(row.bytesInFlight)}</Td>
              <Td className="text-right tabular-nums">{formatUptime(Math.floor(row.ageMs / 1000))}</Td>
              <Td className="text-right tabular-nums">{row.progressAgeMs == null ? '—' : `${(row.progressAgeMs / 1000).toFixed(1)} s ago`}</Td>
            </Tr>
          ))}
          {rows.length === 0 && (
            <Tr className="hover:bg-transparent">
              <Td className="text-gray-500" colSpan={8}>{frame ? 'No transactions in flight' : 'Waiting for telemetry…'}</Td>
            </Tr>
          )}
        </Tbody>
      </Table>
    </div>
  )
}

function clientOf(row: TransactionRow): string {
  if (row.clientPeerId === 0) return '— (left)'
  return row.clientName ? `${peerId(row.clientPeerId)} ${row.clientName}` : peerId(row.clientPeerId)
}
