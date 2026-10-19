import { useState } from 'react'
import { api, type AclLevel } from '../api'
import { useAction, usePolling } from '../hooks'
import { Button } from './ui/button'
import { Dialog, DialogContent, DialogFooter, DialogHeader, DialogTitle } from './ui/dialog'
import { Input } from './ui/input'
import { selectClass } from '../lib'
import { cn } from '../lib/utils'
import { AgentOptions, InterfaceOptions, SubjectOptions } from './ui/acl-fields'

// Without an agent and interface the dialog asks for them too: granting a
// client the hub turned away, before it has opened anything.
export function GrantDialog({ agentName, interfaceName, fingerprintHex, onClose }: {
  agentName?: string
  interfaceName?: string
  fingerprintHex?: string
  onClose: () => void
}) {
  const peers = usePolling(api.peers)
  const interfaces = usePolling(api.interfaces)
  const action = useAction(onClose)
  const [fingerprint, setFingerprint] = useState(fingerprintHex || '*')
  const [agent, setAgent] = useState(agentName || '*')
  const [iface, setIface] = useState(interfaceName || '*')
  const [level, setLevel] = useState<AclLevel>('r')
  const objectFixed = agentName !== undefined && interfaceName !== undefined

  const object = `${agent || '*'}/${iface || '*'}`

  const grant = (event: React.FormEvent) => {
    event.preventDefault()
    action.run(() => api.aclSet(fingerprint || '*', agent || '*', iface || '*', level), `Granted ${level} on ${object}`)
  }

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent>
        <DialogHeader><DialogTitle>{objectFixed ? `Grant access · ${object}` : 'Grant access'}</DialogTitle></DialogHeader>
        <form className="space-y-3" onSubmit={grant}>
          {action.error && <p className="text-sm text-red-600">{action.error}</p>}
          <div className="space-y-1">
            <label className="block text-sm font-medium text-gray-800" htmlFor="grant-subject">Client fingerprint</label>
            <Input
              id="grant-subject"
              list="grant-subjects"
              value={fingerprint}
              onChange={(e) => setFingerprint(e.target.value)}
            />
            <SubjectOptions id="grant-subjects" peers={peers.data} />
            <p className="text-xs text-gray-500">* grants to every client that has no rule of its own.</p>
          </div>
          {!objectFixed && (
            <div className="space-y-1">
              <label className="block text-sm font-medium text-gray-800" htmlFor="grant-agent">Interface</label>
              <div className="flex items-center gap-2">
                <Input id="grant-agent" list="grant-agents" placeholder="agent or *" value={agent} onChange={(e) => setAgent(e.target.value)} />
                <AgentOptions id="grant-agents" interfaces={interfaces.data} />
                <span className="text-gray-500">/</span>
                <Input list="grant-interfaces" placeholder="iface or *" value={iface} onChange={(e) => setIface(e.target.value)} />
                <InterfaceOptions id="grant-interfaces" interfaces={interfaces.data} agent={agent} />
              </div>
            </div>
          )}
          <div className="space-y-1">
            <label className="block text-sm font-medium text-gray-800" htmlFor="grant-level">Level</label>
            <select id="grant-level" className={cn(selectClass, 'w-full')} value={level} onChange={(e) => setLevel(e.target.value as AclLevel)}>
              <option value="none">none — deny read and write</option>
              <option value="r">r — read only</option>
              <option value="rw">rw — read and write</option>
              <option value="rwa">rwa — read, write and configure the bus</option>
            </select>
          </div>
          <DialogFooter>
            <Button type="button" variant="outline" onClick={onClose}>Cancel</Button>
            <Button type="submit" disabled={action.pending}>{action.pending ? 'Granting…' : 'Grant'}</Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  )
}
