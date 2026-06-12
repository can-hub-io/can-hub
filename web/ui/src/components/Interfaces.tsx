import { useState } from 'react'
import { api, PERMISSION, type IfconfigOp } from '../api'
import { useAction, usePolling } from '../hooks'
import { can } from '../lib'

export function Interfaces({ permissions }: { permissions: string[] }) {
  const { data, error, refresh } = usePolling(api.interfaces)
  const action = useAction(refresh)
  const allowConfig = can(permissions, PERMISSION.interfacesConfig)
  const [selected, setSelected] = useState('')
  const [op, setOp] = useState<IfconfigOp>('bitrate')
  const [bitrate, setBitrate] = useState(500000)

  const apply = () => {
    const iface = (data ?? []).find((i) => `${i.agentName}/${i.interfaceName}` === selected)
    if (!iface) {
      action.setError('select an interface')
      return
    }
    action.run(() => api.interfaceConfig(iface.agentName, iface.interfaceName, op, bitrate))
  }

  return (
    <section>
      {error && <p className="error">{error}</p>}
      {action.error && <p className="error">{action.error}</p>}
      {allowConfig && (
        <div className="form">
          <select value={selected} onChange={(e) => setSelected(e.target.value)}>
            <option value="">interface…</option>
            {(data ?? []).map((i) => {
              const name = `${i.agentName}/${i.interfaceName}`
              return <option key={i.interfaceId} value={name}>{name}</option>
            })}
          </select>
          <select value={op} onChange={(e) => setOp(e.target.value as IfconfigOp)}>
            <option value="bitrate">set bitrate</option>
            <option value="up">link up</option>
            <option value="down">link down</option>
          </select>
          {op === 'bitrate' && (
            <input type="number" value={bitrate} onChange={(e) => setBitrate(Number(e.target.value))} style={{ width: 110 }} />
          )}
          <button onClick={apply}>Apply</button>
        </div>
      )}

      <table>
        <thead>
          <tr><th>Interface</th><th className="num">Subscribers</th><th className="num">Frames</th></tr>
        </thead>
        <tbody>
          {(data ?? []).map((i) => (
            <tr key={i.interfaceId}>
              <td>{i.agentName}/{i.interfaceName}</td>
              <td className="num">{i.subscriberCount}</td>
              <td className="num">{i.framesReceived.toLocaleString()}</td>
            </tr>
          ))}
          {data && data.length === 0 && <tr><td colSpan={3}>None.</td></tr>}
        </tbody>
      </table>
    </section>
  )
}
