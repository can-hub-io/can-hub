import { useState } from 'react'
import { api } from '../api'
import { useAction, usePolling } from '../hooks'
import { shortFp } from '../lib'
import { Button } from './ui/button'
import { ConfirmButton } from './ui/confirm'
import { Input } from './ui/input'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

export function Pins() {
  const { data, error, refresh } = usePolling(api.pins)
  const action = useAction(refresh)
  const [name, setName] = useState('')
  const [fingerprint, setFingerprint] = useState('')

  const add = (event: React.FormEvent) => {
    event.preventDefault()
    if (!name || !fingerprint) return action.setError('name and fingerprint required')
    action.run(async () => {
      await api.pinAdd(name, fingerprint)
      setName('')
      setFingerprint('')
    }, `Pinned ${name}`)
  }

  return (
    <div className="space-y-4">
      {error && <p className="text-sm text-red-600">{error}</p>}
      {action.error && <p className="text-sm text-red-600">{action.error}</p>}
      <form className="flex flex-wrap items-center gap-2" onSubmit={add}>
        <Input className="w-48" placeholder="agent name" value={name} onChange={(e) => setName(e.target.value)} />
        <Input className="w-80" placeholder="fingerprint (sha256 hex)" value={fingerprint} onChange={(e) => setFingerprint(e.target.value)} />
        <Button type="submit" disabled={action.pending}>{action.pending ? 'Adding…' : 'Add pin'}</Button>
      </form>

      <Table>
        <Thead>
          <Tr className="hover:bg-transparent"><Th>Agent</Th><Th>Fingerprint</Th><Th></Th></Tr>
        </Thead>
        <Tbody>
          {(data ?? []).map((p) => (
            <Tr key={p.agentName}>
              <Td className="font-medium text-gray-900">{p.agentName}</Td>
              <Td className="font-mono text-xs">{shortFp(p.fingerprintHex)}</Td>
              <Td>
                <div className="flex justify-end">
                  <ConfirmButton
                    variant="outline"
                    size="sm"
                    disabled={action.pending}
                    title={`Delete pin for ${p.agentName}?`}
                    description="The agent will be able to pin a new fingerprint on its next connection."
                    confirmLabel="Delete"
                    destructive
                    onConfirm={() => action.run(() => api.pinDelete(p.agentName), `Pin for ${p.agentName} deleted`)}
                  >
                    Delete
                  </ConfirmButton>
                </div>
              </Td>
            </Tr>
          ))}
          {data && data.length === 0 && (
            <Tr className="hover:bg-transparent"><Td className="text-gray-500" colSpan={3}>None.</Td></Tr>
          )}
        </Tbody>
      </Table>
    </div>
  )
}
