import { type ReactNode } from 'react'
import { useAction, usePolling } from '../hooks'

export interface Column<T> {
  header: string
  render: (row: T) => ReactNode
  num?: boolean
}

// A polled table. `actions`, when given, renders a trailing per-row cell and
// receives `run` to execute a mutating action with inline error reporting.
export function DataView<T>({ fetcher, columns, rowKey, actions }: {
  fetcher: () => Promise<T[]>
  columns: Column<T>[]
  rowKey: (row: T) => string | number
  actions?: (row: T, run: (action: () => Promise<void>) => void) => ReactNode
}) {
  const { data, error, refresh } = usePolling(fetcher)
  const action = useAction(refresh)
  if (error) return <p className="error">{error}</p>
  if (!data) return <p>Loading…</p>
  if (data.length === 0) return <p>None.</p>
  return (
    <section>
      {action.error && <p className="error">{action.error}</p>}
      <table>
        <thead>
          <tr>
            {columns.map((c) => <th key={c.header} className={c.num ? 'num' : ''}>{c.header}</th>)}
            {actions && <th></th>}
          </tr>
        </thead>
        <tbody>
          {data.map((row) => (
            <tr key={rowKey(row)}>
              {columns.map((c) => <td key={c.header} className={c.num ? 'num' : ''}>{c.render(row)}</td>)}
              {actions && <td className="num">{actions(row, action.run)}</td>}
            </tr>
          ))}
        </tbody>
      </table>
    </section>
  )
}
