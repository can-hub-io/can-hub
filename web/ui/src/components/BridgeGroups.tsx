import { useState } from 'react'
import { api, type BridgeGroup, type Interface } from '../api'
import { useAction, usePolling } from '../hooks'
import { endpointOf, filtersOf, nameOf, textOf } from '../lib/bridges'
import { InterfaceNames } from './Bridges'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { ConfirmButton } from './ui/confirm'
import { Dialog, DialogContent, DialogFooter, DialogHeader, DialogTitle } from './ui/dialog'
import { Input } from './ui/input'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

export function BridgeGroups({ interfaces }: { interfaces: Interface[] | null }) {
  const { data, error, refresh } = usePolling(api.bridgeGroups)
  const action = useAction(refresh)
  const [name, setName] = useState('')
  const [ids, setIds] = useState('')
  const [editing, setEditing] = useState<string | null>(null)
  const edited = (data ?? []).find((group) => group.name === editing) ?? null

  const create = (event: React.FormEvent) => {
    event.preventDefault()
    const filters = filtersOf(ids)
    if (!name.trim()) return action.setError('Name the group')
    if (typeof filters === 'string') return action.setError(filters)
    action.run(() => api.bridgeGroupSet(name.trim(), filters), `Group ${name.trim()} saved`)
  }

  return (
    <section className="space-y-4">
      <h2 className="text-base font-semibold text-gray-900">Groups</h2>
      {error && <p className="text-sm text-red-600">{error}</p>}
      {action.error && <p className="text-sm text-red-600">{action.error}</p>}
      <p className="text-sm text-gray-600">
        The buses of a group behave as one: a frame captured on any member is injected into every other member, never
        back into the one it came from. Saving an existing group replaces its ids and keeps its members. Clients
        also open a group as the interface group/&lt;name&gt; to read every member and write into all of them.
      </p>
      <form className="flex flex-wrap items-center gap-2" onSubmit={create}>
        <Input className="w-48" placeholder="group name" value={name} onChange={(e) => setName(e.target.value)} />
        <Input
          className="w-64"
          placeholder="hex ids: 100 200:700 (empty = all)"
          value={ids}
          onChange={(e) => setIds(e.target.value)}
        />
        <Button type="submit" disabled={action.pending}>{action.pending ? 'Saving…' : 'Save group'}</Button>
      </form>

      <Table>
        <Thead>
          <Tr className="hover:bg-transparent">
            <Th>Group</Th><Th>Members</Th><Th>Ids</Th><Th>Forwarded</Th><Th>Dropped</Th><Th></Th>
          </Tr>
        </Thead>
        <Tbody>
          {(data ?? []).map((group) => (
            <Tr key={group.name}>
              <Td className="font-medium">{group.name}</Td>
              <Td>
                <div className="flex flex-wrap gap-1">
                  {group.members.map((member) => (
                    <Badge key={nameOf(member)} variant={member.online ? 'success' : 'warning'}>{nameOf(member)}</Badge>
                  ))}
                  {group.members.length === 0 && <span className="text-gray-500">none yet</span>}
                </div>
              </Td>
              <Td className="font-mono text-xs">{textOf(group.filters)}</Td>
              <Td>{group.forwarded}</Td>
              <Td>{group.dropped}</Td>
              <Td>
                <div className="flex justify-end gap-2">
                  <Button variant="outline" size="sm" onClick={() => setEditing(group.name)}>Members…</Button>
                  <ConfirmButton
                    variant="outline"
                    size="sm"
                    disabled={action.pending}
                    title="Delete this group?"
                    description={`Its ${group.members.length} buses stop feeding each other.`}
                    confirmLabel="Delete"
                    destructive
                    onConfirm={() => action.run(() => api.bridgeGroupDelete(group.name), 'Group deleted')}
                  >
                    Delete
                  </ConfirmButton>
                </div>
              </Td>
            </Tr>
          ))}
          {data && data.length === 0 && (
            <Tr className="hover:bg-transparent"><Td className="text-gray-500" colSpan={6}>None.</Td></Tr>
          )}
        </Tbody>
      </Table>
      {edited && (
        <MembersDialog group={edited} interfaces={interfaces} onChanged={refresh} onClose={() => setEditing(null)} />
      )}
    </section>
  )
}

function MembersDialog({ group, interfaces, onChanged, onClose }: {
  group: BridgeGroup
  interfaces: Interface[] | null
  onChanged: () => void
  onClose: () => void
}) {
  const action = useAction(onChanged)
  const [member, setMember] = useState('')

  const join = (event: React.FormEvent) => {
    event.preventDefault()
    const endpoint = endpointOf(member)
    if (!endpoint) return action.setError('Name the bus as agent/interface')
    action.run(() => api.bridgeGroupJoin(group.name, endpoint), `${member} joined ${group.name}`)
    setMember('')
  }

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="w-[calc(100%-2rem)] max-w-lg">
        <DialogHeader>
          <DialogTitle>Members · {group.name}</DialogTitle>
        </DialogHeader>
        {action.error && <p className="text-sm text-red-600">{action.error}</p>}
        <ul className="divide-y divide-gray-100 rounded-md border border-gray-200">
          {group.members.map((m) => (
            <li key={nameOf(m)} className="flex items-center justify-between px-3 py-2 text-sm">
              <span className="flex items-center gap-2">
                {nameOf(m)}
                <Badge variant={m.online ? 'success' : 'warning'}>{m.online ? 'online' : 'offline'}</Badge>
              </span>
              <Button
                variant="outline"
                size="sm"
                disabled={action.pending}
                onClick={() => action.run(() => api.bridgeGroupLeave(group.name, m), `${nameOf(m)} left ${group.name}`)}
              >
                Remove
              </Button>
            </li>
          ))}
          {group.members.length === 0 && <li className="px-3 py-2 text-sm text-gray-500">No members yet.</li>}
        </ul>
        <form className="flex gap-2" onSubmit={join}>
          <Input
            className="flex-1"
            list="group-member-interfaces"
            placeholder="agent/iface"
            value={member}
            onChange={(e) => setMember(e.target.value)}
          />
          <InterfaceNames id="group-member-interfaces" interfaces={interfaces} />
          <Button type="submit" disabled={action.pending}>Add</Button>
        </form>
        <DialogFooter>
          <Button type="button" variant="outline" onClick={onClose}>Done</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}
