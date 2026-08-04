import { useState } from 'react'
import { api, type AclLevel } from '../api'
import { useAction, usePolling } from '../hooks'
import { middleFp, selectClass, unknownObject } from '../lib'
import { Fingerprint } from './ui/fingerprint'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { ConfirmButton } from './ui/confirm'
import { Input } from './ui/input'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'
import { AgentOptions, InterfaceOptions, SubjectOptions } from './ui/acl-fields'

const levelVariant: Record<string, 'secondary' | 'primary' | 'warning'> = {
  none: 'secondary',
  ro: 'primary',
  rw: 'warning',
}

export function Acls() {
  const { data, error, refresh } = usePolling(api.acls)
  const interfaces = usePolling(api.interfaces)
  const peers = usePolling(api.peers)
  const action = useAction(refresh)
  const [fingerprint, setFingerprint] = useState('*')
  const [agent, setAgent] = useState('*')
  const [iface, setIface] = useState('*')
  const [level, setLevel] = useState<AclLevel>('ro')

  const grant = (event: React.FormEvent) => {
    event.preventDefault()
    action.run(
      () => api.aclSet(fingerprint || '*', agent || '*', iface || '*', level),
      `Granted ${level} on ${agent || '*'}/${iface || '*'}`,
    )
  }

  return (
    <div className="space-y-4">
      {error && <p className="text-sm text-red-600">{error}</p>}
      {action.error && <p className="text-sm text-red-600">{action.error}</p>}
      <form className="flex flex-wrap items-center gap-2" onSubmit={grant}>
        <Input
          className="w-64"
          list="acl-subjects"
          placeholder="fingerprint or *"
          value={fingerprint}
          onChange={(e) => setFingerprint(e.target.value)}
        />
        <SubjectOptions id="acl-subjects" peers={peers.data} />
        <Input
          className="w-40"
          list="acl-agents"
          placeholder="agent or *"
          value={agent}
          onChange={(e) => setAgent(e.target.value)}
        />
        <AgentOptions id="acl-agents" interfaces={interfaces.data} />
        <Input
          className="w-40"
          list="acl-interfaces"
          placeholder="iface or *"
          value={iface}
          onChange={(e) => setIface(e.target.value)}
        />
        <InterfaceOptions id="acl-interfaces" interfaces={interfaces.data} agent={agent} />
        <select className={selectClass} value={level} onChange={(e) => setLevel(e.target.value as AclLevel)}>
          <option value="none">none</option>
          <option value="ro">ro</option>
          <option value="rw">rw</option>
        </select>
        <Button type="submit" disabled={action.pending}>{action.pending ? 'Granting…' : 'Grant'}</Button>
      </form>
      {unknownObject(interfaces.data, agent, iface) && (
        <p className="text-sm text-amber-700">
          No interface called {agent}/{iface} is connected right now. The grant still applies if one appears — check the
          spelling if you did not mean to pre-authorise it.
        </p>
      )}

      <Table>
        <Thead>
          <Tr className="hover:bg-transparent"><Th>Fingerprint</Th><Th>Object</Th><Th>Level</Th><Th></Th></Tr>
        </Thead>
        <Tbody>
          {(data ?? []).map((a) => (
            <Tr key={`${a.fingerprintHex}-${a.agentName}-${a.interfaceName}`}>
              <Td><Fingerprint value={a.fingerprintHex} full /></Td>
              <Td>{a.agentName}/{a.interfaceName}</Td>
              <Td><Badge variant={levelVariant[a.level] ?? 'secondary'}>{a.level}</Badge></Td>
              <Td>
                <div className="flex justify-end">
                  <ConfirmButton
                    variant="outline"
                    size="sm"
                    disabled={action.pending}
                    title="Revoke this grant?"
                    description={`${middleFp(a.fingerprintHex)} on ${a.agentName}/${a.interfaceName}. Live sessions keep the access they opened with.`}
                    confirmLabel="Revoke"
                    destructive
                    onConfirm={() => action.run(() => api.aclRevoke(a.fingerprintHex, a.agentName, a.interfaceName), 'Grant revoked')}
                  >
                    Revoke
                  </ConfirmButton>
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
