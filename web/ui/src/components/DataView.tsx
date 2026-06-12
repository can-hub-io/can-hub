import { type ReactNode } from 'react'
import { useAction, usePolling } from '../hooks'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

export interface Column<T> {
  header: string
  render: (row: T) => ReactNode
  num?: boolean
}

export function DataView<T>({ fetcher, columns, rowKey, actions }: {
  fetcher: () => Promise<T[]>
  columns: Column<T>[]
  rowKey: (row: T) => string | number
  actions?: (row: T, run: (action: () => Promise<void>) => void) => ReactNode
}) {
  const { data, error, refresh } = usePolling(fetcher)
  const action = useAction(refresh)
  if (error) return <p className="text-sm text-red-600">{error}</p>
  if (!data) return <p className="text-gray-500">Loading…</p>
  if (data.length === 0) return <p className="text-gray-500">None.</p>
  return (
    <div className="space-y-3">
      {action.error && <p className="text-sm text-red-600">{action.error}</p>}
      <Table>
        <Thead>
          <Tr className="hover:bg-transparent">
            {columns.map((c) => <Th key={c.header} className={c.num ? 'text-right' : ''}>{c.header}</Th>)}
            {actions && <Th className="text-right"></Th>}
          </Tr>
        </Thead>
        <Tbody>
          {data.map((row) => (
            <Tr key={rowKey(row)}>
              {columns.map((c) => (
                <Td key={c.header} className={c.num ? 'text-right tabular-nums' : ''}>{c.render(row)}</Td>
              ))}
              {actions && <Td className="text-right"><div className="flex justify-end gap-2">{actions(row, action.run)}</div></Td>}
            </Tr>
          ))}
        </Tbody>
      </Table>
    </div>
  )
}
