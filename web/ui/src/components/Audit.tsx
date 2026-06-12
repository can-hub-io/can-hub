import { api } from '../api'
import { usePolling } from '../hooks'
import { Badge } from './ui/badge'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

function statusVariant(status: number): 'success' | 'warning' | 'destructive' {
  if (status < 400) return 'success'
  if (status < 500) return 'warning'
  return 'destructive'
}

export function Audit() {
  const { data, error } = usePolling(api.listAudit, 5000)
  if (error) return <p className="text-sm text-red-600">{error}</p>
  if (!data) return <p className="text-gray-500">Loading…</p>
  if (data.length === 0) return <p className="text-gray-500">No audited actions yet.</p>
  return (
    <Table>
      <Thead>
        <Tr className="hover:bg-transparent">
          <Th>When</Th><Th>Actor</Th><Th>Action</Th><Th>Target</Th><Th className="text-right">Status</Th>
        </Tr>
      </Thead>
      <Tbody>
        {data.map((entry, index) => (
          <Tr key={index}>
            <Td className="whitespace-nowrap text-gray-500">{new Date(entry.at * 1000).toLocaleString()}</Td>
            <Td className="font-medium text-gray-900">{entry.actor}</Td>
            <Td className="font-mono text-xs">{entry.action}</Td>
            <Td className="font-mono text-xs">{entry.target}</Td>
            <Td className="text-right"><Badge variant={statusVariant(entry.status)}>{entry.status}</Badge></Td>
          </Tr>
        ))}
      </Tbody>
    </Table>
  )
}
