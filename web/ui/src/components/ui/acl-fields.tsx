import { type Interface, type Peer } from '../../api'
import { middleFp } from '../../lib'

// The three ACL fields accept a live value, the "*" wildcard, or something not
// connected yet (pre-authorisation), so they suggest instead of constraining.
export function SubjectOptions({ id, peers }: { id: string; peers: Peer[] | null }) {
  const subjects = (peers ?? []).filter((p) => p.role === 'client' && p.fingerprintHex)

  return (
    <datalist id={id}>
      <option value="*">any client without its own rule</option>
      {subjects.map((p) => (
        <option key={p.fingerprintHex} value={p.fingerprintHex}>
          {p.agentName || middleFp(p.fingerprintHex)}
        </option>
      ))}
    </datalist>
  )
}

export function AgentOptions({ id, interfaces }: { id: string; interfaces: Interface[] | null }) {
  const agents = [...new Set((interfaces ?? []).map((i) => i.agentName))]

  return (
    <datalist id={id}>
      <option value="*">any agent</option>
      {agents.map((name) => <option key={name} value={name} />)}
    </datalist>
  )
}

export function InterfaceOptions({ id, interfaces, agent }: {
  id: string
  interfaces: Interface[] | null
  agent: string
}) {
  const scoped = (interfaces ?? []).filter((i) => agent === '*' || !agent || i.agentName === agent)
  const names = [...new Set(scoped.map((i) => i.interfaceName))]

  return (
    <datalist id={id}>
      <option value="*">any interface</option>
      {names.map((name) => <option key={name} value={name} />)}
    </datalist>
  )
}
