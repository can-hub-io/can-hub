import { useState } from 'react'
import { api, type ManagedGroup, type ManagedUser } from '../api'
import { useAction, usePolling } from '../hooks'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { Input } from './ui/input'
import { Dialog, DialogContent, DialogFooter, DialogHeader, DialogTitle } from './ui/dialog'
import { Table, Tbody, Td, Th, Thead, Tr } from './ui/table'

export function Users({ currentUser }: { currentUser: string | null }) {
  const users = usePolling(api.listManagedUsers)
  const groups = usePolling(api.listManagedGroups)
  const permissions = usePolling(api.listPermissions)
  const action = useAction(() => {
    users.refresh()
    groups.refresh()
  })
  const [userName, setUserName] = useState('')
  const [userPassword, setUserPassword] = useState('')
  const [groupName, setGroupName] = useState('')
  const [editingUser, setEditingUser] = useState<ManagedUser | null>(null)
  const [editingGroup, setEditingGroup] = useState<ManagedGroup | null>(null)

  const groupList = groups.data ?? []
  const permissionList = permissions.data ?? []
  const groupNameOf = (id: number) => groupList.find((g) => g.id === id)?.name ?? `#${id}`

  const addUser = (event: React.FormEvent) => {
    event.preventDefault()
    if (!userName || !userPassword) return action.setError('name and password required')
    action.run(async () => {
      await api.createManagedUser(userName, userPassword)
      setUserName('')
      setUserPassword('')
    })
  }

  const addGroup = (event: React.FormEvent) => {
    event.preventDefault()
    if (!groupName) return action.setError('group name required')
    action.run(async () => {
      await api.createManagedGroup(groupName)
      setGroupName('')
    })
  }

  return (
    <div className="space-y-8">
      {action.error && <p className="text-sm text-red-600">{action.error}</p>}

      <section className="space-y-3">
        <h2 className="text-base font-semibold text-gray-900">Groups</h2>
        <form className="flex flex-wrap items-center gap-2" onSubmit={addGroup}>
          <Input className="w-56" placeholder="group name" value={groupName} onChange={(e) => setGroupName(e.target.value)} />
          <Button type="submit">Add group</Button>
        </form>
        <Table>
          <Thead>
            <Tr className="hover:bg-transparent"><Th>Group</Th><Th>Permissions</Th><Th></Th></Tr>
          </Thead>
          <Tbody>
            {groupList.map((g) => (
              <Tr key={g.id}>
                <Td className="font-medium text-gray-900">{g.name}</Td>
                <Td>
                  {g.permissions.length ? (
                    <div className="flex flex-wrap gap-1">
                      {g.permissions.map((p) => <Badge key={p} variant="secondary" className="font-mono">{p}</Badge>)}
                    </div>
                  ) : (
                    <span className="text-gray-400">none</span>
                  )}
                </Td>
                <Td>
                  <div className="flex justify-end gap-2">
                    <Button variant="outline" size="sm" onClick={() => setEditingGroup(g)}>Edit</Button>
                    <Button
                      variant="outline"
                      size="sm"
                      onClick={() => {
                        if (window.confirm(`Delete group ${g.name}?`)) action.run(() => api.deleteManagedGroup(g.id))
                      }}
                    >
                      Delete
                    </Button>
                  </div>
                </Td>
              </Tr>
            ))}
            {groupList.length === 0 && (
              <Tr className="hover:bg-transparent"><Td className="text-gray-500" colSpan={3}>No groups.</Td></Tr>
            )}
          </Tbody>
        </Table>
      </section>

      <section className="space-y-3">
        <h2 className="text-base font-semibold text-gray-900">Users</h2>
        <form className="flex flex-wrap items-center gap-2" onSubmit={addUser}>
          <Input className="w-56" placeholder="user name" value={userName} onChange={(e) => setUserName(e.target.value)} />
          <Input
            className="w-56"
            placeholder="password"
            type="password"
            autoComplete="new-password"
            value={userPassword}
            onChange={(e) => setUserPassword(e.target.value)}
          />
          <Button type="submit">Add user</Button>
        </form>
        <Table>
          <Thead>
            <Tr className="hover:bg-transparent"><Th>User</Th><Th>Status</Th><Th>Groups</Th><Th></Th></Tr>
          </Thead>
          <Tbody>
            {(users.data ?? []).map((u) => {
              const isSelf = u.name === currentUser
              return (
                <Tr key={u.id}>
                  <Td className="font-medium text-gray-900">
                    {u.name}
                    {isSelf && <span className="ml-2 text-xs font-normal text-gray-400">(you)</span>}
                  </Td>
                  <Td>
                    {u.enabled ? <Badge variant="success">enabled</Badge> : <Badge variant="secondary">disabled</Badge>}
                  </Td>
                  <Td className="text-gray-600">
                    {u.groupIds.length ? u.groupIds.map(groupNameOf).join(', ') : <span className="text-gray-400">none</span>}
                  </Td>
                  <Td>
                    <div className="flex justify-end gap-2">
                      <Button variant="outline" size="sm" onClick={() => setEditingUser(u)}>Edit</Button>
                      {!isSelf && (
                        <Button
                          variant="outline"
                          size="sm"
                          onClick={() => {
                            if (window.confirm(`Delete user ${u.name}?`)) action.run(() => api.deleteManagedUser(u.id))
                          }}
                        >
                          Delete
                        </Button>
                      )}
                    </div>
                  </Td>
                </Tr>
              )
            })}
            {(users.data ?? []).length === 0 && (
              <Tr className="hover:bg-transparent"><Td className="text-gray-500" colSpan={4}>No users.</Td></Tr>
            )}
          </Tbody>
        </Table>
      </section>

      {editingGroup && (
        <EditGroup group={editingGroup} permissions={permissionList} run={action.run} onClose={() => setEditingGroup(null)} />
      )}
      {editingUser && (
        <EditUser
          user={editingUser}
          groups={groupList}
          isSelf={editingUser.name === currentUser}
          run={action.run}
          onClose={() => setEditingUser(null)}
        />
      )}
    </div>
  )
}

function EditGroup({ group, permissions, run, onClose }: {
  group: ManagedGroup
  permissions: string[]
  run: (action: () => Promise<void>) => void
  onClose: () => void
}) {
  const [selected, setSelected] = useState<string[]>(group.permissions)
  const toggle = (p: string) => setSelected((s) => (s.includes(p) ? s.filter((x) => x !== p) : [...s, p]))
  const save = () => {
    run(() => api.setGroupPermissions(group.id, selected))
    onClose()
  }
  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent>
        <DialogHeader><DialogTitle>Edit group · {group.name}</DialogTitle></DialogHeader>
        <div className="space-y-2">
          {permissions.map((p) => (
            <label key={p} className="flex items-center gap-2 text-sm">
              <input type="checkbox" className="accent-primary-600" checked={selected.includes(p)} onChange={() => toggle(p)} />
              <code className="rounded bg-gray-100 px-1.5 py-0.5 text-xs">{p}</code>
            </label>
          ))}
        </div>
        <DialogFooter>
          <Button variant="outline" onClick={onClose}>Cancel</Button>
          <Button onClick={save}>Save</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}

function EditUser({ user, groups, isSelf, run, onClose }: {
  user: ManagedUser
  groups: ManagedGroup[]
  isSelf: boolean
  run: (action: () => Promise<void>) => void
  onClose: () => void
}) {
  const [enabled, setEnabled] = useState(user.enabled)
  const [memberOf, setMemberOf] = useState<number[]>(user.groupIds)
  const toggle = (id: number) => setMemberOf((s) => (s.includes(id) ? s.filter((x) => x !== id) : [...s, id]))

  const save = () => {
    run(async () => {
      if (enabled !== user.enabled) await api.setManagedUserEnabled(user.id, enabled)
      for (const g of groups) {
        const before = user.groupIds.includes(g.id)
        const after = memberOf.includes(g.id)
        if (after && !before) await api.addMembership(user.id, g.id)
        if (!after && before) await api.removeMembership(user.id, g.id)
      }
    })
    onClose()
  }

  const resetPassword = () => {
    const password = window.prompt(`New password for ${user.name} (min 8 chars)`)
    if (password) run(() => api.resetUserPassword(user.id, password))
  }

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent>
        <DialogHeader><DialogTitle>Edit user · {user.name}</DialogTitle></DialogHeader>
        <label className="flex items-center gap-2 text-sm font-medium text-gray-800">
          <input
            type="checkbox"
            className="accent-primary-600 disabled:opacity-50"
            checked={enabled}
            disabled={isSelf}
            onChange={(e) => setEnabled(e.target.checked)}
          />
          Enabled{isSelf && <span className="text-xs font-normal text-gray-400">(can't disable yourself)</span>}
        </label>
        <div className="mt-4 text-xs font-semibold uppercase tracking-wide text-gray-400">Groups</div>
        <div className="mt-2 space-y-2">
          {groups.map((g) => (
            <label key={g.id} className="flex items-center gap-2 text-sm">
              <input type="checkbox" className="accent-primary-600" checked={memberOf.includes(g.id)} onChange={() => toggle(g.id)} />
              {g.name}
            </label>
          ))}
          {groups.length === 0 && <span className="text-sm text-gray-400">No groups yet.</span>}
        </div>
        <DialogFooter className="justify-between">
          <Button variant="outline" onClick={resetPassword}>Reset password</Button>
          <div className="flex gap-2">
            <Button variant="outline" onClick={onClose}>Cancel</Button>
            <Button onClick={save}>Save</Button>
          </div>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}
