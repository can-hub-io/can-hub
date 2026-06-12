import { api } from '../api'
import { usePolling } from '../hooks'

export function Audit() {
  const { data, error } = usePolling(api.listAudit, 5000)
  if (error) return <p className="error">{error}</p>
  if (!data) return <p>Loading…</p>
  if (data.length === 0) return <p>No audited actions yet.</p>
  return (
    <table>
      <thead>
        <tr><th>When</th><th>Actor</th><th>Action</th><th>Target</th><th className="num">Status</th></tr>
      </thead>
      <tbody>
        {data.map((entry, index) => (
          <tr key={index}>
            <td>{new Date(entry.at * 1000).toLocaleString()}</td>
            <td>{entry.actor}</td>
            <td>{entry.action}</td>
            <td>{entry.target}</td>
            <td className="num">{entry.status}</td>
          </tr>
        ))}
      </tbody>
    </table>
  )
}
