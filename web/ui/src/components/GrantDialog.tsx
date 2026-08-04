import { useState } from 'react'
import { api, type AclLevel } from '../api'
import { useAction, usePolling } from '../hooks'
import { Button } from './ui/button'
import { Dialog, DialogContent, DialogFooter, DialogHeader, DialogTitle } from './ui/dialog'
import { Input } from './ui/input'
import { selectClass } from '../lib'
import { cn } from '../lib/utils'
import { SubjectOptions } from './ui/acl-fields'

export function GrantDialog({ agentName, interfaceName, fingerprintHex, onClose }: {
  agentName: string
  interfaceName: string
  fingerprintHex?: string
  onClose: () => void
}) {
  const peers = usePolling(api.peers)
  const action = useAction(onClose)
  const [fingerprint, setFingerprint] = useState(fingerprintHex || '*')
  const [level, setLevel] = useState<AclLevel>('ro')

  const object = `${agentName}/${interfaceName}`

  const grant = (event: React.FormEvent) => {
    event.preventDefault()
    action.run(() => api.aclSet(fingerprint || '*', agentName, interfaceName, level), `Granted ${level} on ${object}`)
  }

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent>
        <DialogHeader><DialogTitle>Grant access · {object}</DialogTitle></DialogHeader>
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
          <div className="space-y-1">
            <label className="block text-sm font-medium text-gray-800" htmlFor="grant-level">Level</label>
            <select id="grant-level" className={cn(selectClass, 'w-full')} value={level} onChange={(e) => setLevel(e.target.value as AclLevel)}>
              <option value="none">none — deny read and write</option>
              <option value="ro">ro — read only</option>
              <option value="rw">rw — read and write</option>
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
