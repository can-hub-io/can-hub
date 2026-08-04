import { useState } from 'react'
import { api } from '../api'
import { useAction, usePolling } from '../hooks'
import { Fingerprint } from './ui/fingerprint'
import { Button } from './ui/button'
import { ConfirmButton } from './ui/confirm'
import { Input } from './ui/input'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

const FINGERPRINT_HEX_LENGTH = 64

const isFingerprint = (text: string) => new RegExp(`^[0-9a-f]{${FINGERPRINT_HEX_LENGTH}}$`).test(text.toLowerCase())

export function Pins() {
  const { data, error, refresh } = usePolling(api.pins)
  const action = useAction(refresh)
  const [name, setName] = useState('')
  const [fingerprint, setFingerprint] = useState('')

  const add = (event: React.FormEvent) => {
    event.preventDefault()
    if (!name || !fingerprint) return action.setError('name and fingerprint required')
    if (!isFingerprint(fingerprint)) {
      return action.setError(`a fingerprint is ${FINGERPRINT_HEX_LENGTH} hex characters; this one has ${fingerprint.length}`)
    }
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
      <p className="text-xs text-gray-500">
        Pin an agent you have not seen yet. For one that is already connected, use the Pin button on its Agents row.
      </p>

      <Table>
        <Thead>
          <Tr className="hover:bg-transparent"><Th>Agent</Th><Th>Fingerprint</Th><Th></Th></Tr>
        </Thead>
        <Tbody>
          {(data ?? []).map((p) => (
            <Tr key={p.agentName}>
              <Td className="font-medium text-gray-900">{p.agentName}</Td>
              <Td><Fingerprint value={p.fingerprintHex} full /></Td>
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
