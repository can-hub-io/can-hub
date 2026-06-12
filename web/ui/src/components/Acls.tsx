import { useState } from 'react'
import { api, type AclLevel } from '../api'
import { useAction, usePolling } from '../hooks'
import { shortFp } from '../lib'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { Input } from './ui/input'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

const selectClass =
  'h-9 rounded-md border border-gray-300 bg-white px-3 text-sm text-gray-900 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary-500/40'

const levelVariant: Record<string, 'secondary' | 'primary' | 'warning'> = {
  none: 'secondary',
  ro: 'primary',
  rw: 'warning',
}

export function Acls() {
  const { data, error, refresh } = usePolling(api.acls)
  const action = useAction(refresh)
  const [fingerprint, setFingerprint] = useState('*')
  const [agent, setAgent] = useState('*')
  const [iface, setIface] = useState('*')
  const [level, setLevel] = useState<AclLevel>('ro')

  const grant = (event: React.FormEvent) => {
    event.preventDefault()
    action.run(() => api.aclSet(fingerprint || '*', agent || '*', iface || '*', level))
  }

  return (
    <div className="space-y-4">
      {error && <p className="text-sm text-red-600">{error}</p>}
      {action.error && <p className="text-sm text-red-600">{action.error}</p>}
      <form className="flex flex-wrap items-center gap-2" onSubmit={grant}>
        <Input className="w-64" placeholder="fingerprint or *" value={fingerprint} onChange={(e) => setFingerprint(e.target.value)} />
        <Input className="w-40" placeholder="agent or *" value={agent} onChange={(e) => setAgent(e.target.value)} />
        <Input className="w-40" placeholder="iface or *" value={iface} onChange={(e) => setIface(e.target.value)} />
        <select className={selectClass} value={level} onChange={(e) => setLevel(e.target.value as AclLevel)}>
          <option value="none">none</option>
          <option value="ro">ro</option>
          <option value="rw">rw</option>
        </select>
        <Button type="submit">Grant</Button>
      </form>

      <Table>
        <Thead>
          <Tr className="hover:bg-transparent"><Th>Fingerprint</Th><Th>Object</Th><Th>Level</Th><Th></Th></Tr>
        </Thead>
        <Tbody>
          {(data ?? []).map((a) => (
            <Tr key={`${a.fingerprintHex}-${a.agentName}-${a.interfaceName}`}>
              <Td className="font-mono text-xs">{shortFp(a.fingerprintHex)}</Td>
              <Td>{a.agentName}/{a.interfaceName}</Td>
              <Td><Badge variant={levelVariant[a.level] ?? 'secondary'}>{a.level}</Badge></Td>
              <Td>
                <div className="flex justify-end">
                  <Button
                    variant="outline"
                    size="sm"
                    onClick={() => {
                      if (window.confirm(`Revoke grant for ${shortFp(a.fingerprintHex)} on ${a.agentName}/${a.interfaceName}?`))
                        action.run(() => api.aclRevoke(a.fingerprintHex, a.agentName, a.interfaceName))
                    }}
                  >
                    Revoke
                  </Button>
                </div>
              </Td>
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
