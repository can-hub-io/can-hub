import { useState } from 'react'
import { api } from '../api'
import { useAction, usePolling } from '../hooks'

export function Users() {
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

  const addUser = () => {
    if (!userName || !userPassword) {
      action.setError('name and password required')
      return
    }
    action.run(async () => {
      await api.createManagedUser(userName, userPassword)
      setUserName('')
      setUserPassword('')
    })
  }

  const addGroup = () => {
    if (!groupName) {
      action.setError('group name required')
      return
    }
    action.run(async () => {
      await api.createManagedGroup(groupName)
      setGroupName('')
    })
  }

  const groupList = groups.data ?? []
  const permissionList = permissions.data ?? []

  return (
    <section>
      {action.error && <p className="error">{action.error}</p>}
      <h2>Groups</h2>
      <div className="form">
        <input placeholder="group name" value={groupName} onChange={(e) => setGroupName(e.target.value)} />
        <button onClick={addGroup}>Add group</button>
      </div>
      <table>
        <thead>
          <tr><th>Group</th>{permissionList.map((p) => <th key={p}>{p}</th>)}<th></th></tr>
        </thead>
        <tbody>
          {groupList.map((g) => (
            <tr key={g.id}>
              <td>{g.name}</td>
              {permissionList.map((p) => {
                const has = g.permissions.includes(p)
                const next = has ? g.permissions.filter((x) => x !== p) : [...g.permissions, p]
                return (
                  <td key={p} className="num">
                    <input
                      type="checkbox"
                      checked={has}
                      onChange={() => action.run(() => api.setGroupPermissions(g.id, next))}
                    />
                  </td>
                )
              })}
              <td className="num">
                <button
                  onClick={() => {
                    if (window.confirm(`Delete group ${g.name}?`)) action.run(() => api.deleteManagedGroup(g.id))
                  }}
                >
                  Delete
                </button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>

      <h2>Users</h2>
      <div className="form">
        <input placeholder="user name" value={userName} onChange={(e) => setUserName(e.target.value)} />
        <input placeholder="password" type="password" autoComplete="new-password" value={userPassword} onChange={(e) => setUserPassword(e.target.value)} />
        <button onClick={addUser}>Add user</button>
      </div>
      <table>
        <thead>
          <tr><th>User</th><th>Enabled</th>{groupList.map((g) => <th key={g.id}>{g.name}</th>)}<th></th></tr>
        </thead>
        <tbody>
          {(users.data ?? []).map((u) => (
            <tr key={u.id}>
              <td>{u.name}</td>
              <td className="num">
                <input
                  type="checkbox"
                  checked={u.enabled}
                  onChange={() => action.run(() => api.setManagedUserEnabled(u.id, !u.enabled))}
                />
              </td>
              {groupList.map((g) => {
                const member = u.groupIds.includes(g.id)
                return (
                  <td key={g.id} className="num">
                    <input
                      type="checkbox"
                      checked={member}
                      onChange={() =>
                        action.run(() => (member ? api.removeMembership(u.id, g.id) : api.addMembership(u.id, g.id)))
                      }
                    />
                  </td>
                )
              })}
              <td className="num">
                <button
                  onClick={() => {
                    const password = window.prompt(`New password for ${u.name} (min 8 chars)`)
                    if (password) action.run(() => api.resetUserPassword(u.id, password))
                  }}
                >
                  Reset pw
                </button>{' '}
                <button
                  onClick={() => {
                    if (window.confirm(`Delete user ${u.name}?`)) action.run(() => api.deleteManagedUser(u.id))
                  }}
                >
                  Delete
                </button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </section>
  )
}
