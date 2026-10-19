import { useEffect, useState } from 'react'
import { api, CAN_MODES, type CanMode, type InterfaceSettings, type InterfaceSettingsChange } from '../api'
import { useAction } from '../hooks'
import { selectClass } from '../lib'
import { cn } from '../lib/utils'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { Dialog, DialogContent, DialogFooter, DialogHeader, DialogTitle } from './ui/dialog'
import { Input } from './ui/input'

const MODE_LABELS: Record<CanMode, string> = {
  fd: 'CAN FD',
  listenOnly: 'Listen-only',
  loopback: 'Loopback',
  oneShot: 'One-shot',
  tripleSampling: 'Triple sampling',
  berrReporting: 'Bus-error reporting',
}

type NumberKey = 'bitrate' | 'samplePoint' | 'dataBitrate' | 'dataSamplePoint' | 'restartMs' | 'txqueuelen'

const NUMBER_FIELDS: { key: NumberKey; label: string; placeholder: string }[] = [
  { key: 'bitrate', label: 'Bitrate (bit/s)', placeholder: '500000' },
  { key: 'samplePoint', label: 'Sample point', placeholder: '0.875' },
  { key: 'dataBitrate', label: 'Data bitrate (bit/s)', placeholder: '2000000' },
  { key: 'dataSamplePoint', label: 'Data sample point', placeholder: '0.75' },
  { key: 'restartMs', label: 'Restart after bus-off (ms)', placeholder: '0 = never' },
  { key: 'txqueuelen', label: 'TX queue length', placeholder: '10' },
]

type NumberTexts = Record<NumberKey, string>

// Only what the user changed goes to the hub, so a field the device does not
// report (a vcan has no bit timing) is never sent back by accident.
export function ConfigureDialog({ agentName, interfaceName, onClose }: {
  agentName: string
  interfaceName: string
  onClose: () => void
}) {
  const action = useAction(onClose)
  const [current, setCurrent] = useState<InterfaceSettings | null>(null)
  const [loadError, setLoadError] = useState<string | null>(null)
  const [numbers, setNumbers] = useState<NumberTexts>(textsOf(null))
  const [modes, setModes] = useState<Partial<Record<CanMode, boolean>>>({})
  const [up, setUp] = useState<boolean | undefined>(undefined)
  const name = `${agentName}/${interfaceName}`

  useEffect(() => {
    api.interfaceSettings(agentName, interfaceName)
      .then((settings) => {
        setCurrent(settings)
        setNumbers(textsOf(settings))
        setModes(settings.modes)
        setUp(settings.up)
      })
      .catch((cause) => setLoadError(cause instanceof Error ? cause.message : String(cause)))
  }, [agentName, interfaceName])

  const apply = (event: React.FormEvent) => {
    event.preventDefault()
    if (!current) return
    const change = changeFrom(current, numbers, modes, up)
    if (typeof change === 'string') return action.setError(change)
    action.run(() => api.interfaceConfig(agentName, interfaceName, change), `${name} reconfigured`)
  }

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="max-h-[90vh] w-[calc(100%-2rem)] max-w-lg overflow-y-auto">
        <DialogHeader>
          <DialogTitle>Configure · {name}</DialogTitle>
          {current?.state && (
            <div><Badge variant={current.state === 'error-active' ? 'success' : 'warning'}>{current.state}</Badge></div>
          )}
        </DialogHeader>
        {loadError && <p className="text-sm text-red-600">{loadError}</p>}
        {!current && !loadError && <p className="text-sm text-gray-500">Reading the interface…</p>}
        {current && (
          <form className="space-y-4" onSubmit={apply}>
            {action.error && <p className="text-sm text-red-600">{action.error}</p>}
            <div className="space-y-1">
              <label className="block text-sm font-medium text-gray-800" htmlFor="configure-link">Link</label>
              <select
                id="configure-link"
                className={cn(selectClass, 'w-full')}
                value={up === undefined ? '' : up ? 'up' : 'down'}
                onChange={(e) => setUp(e.target.value === 'up')}
              >
                {up === undefined && <option value="">unknown</option>}
                <option value="up">up</option>
                <option value="down">down</option>
              </select>
            </div>
            <div className="grid gap-3 sm:grid-cols-2">
              {NUMBER_FIELDS.map((field) => (
                <div className="space-y-1" key={field.key}>
                  <label className="block text-sm font-medium text-gray-800" htmlFor={`configure-${field.key}`}>
                    {field.label}
                  </label>
                  <Input
                    id={`configure-${field.key}`}
                    inputMode="decimal"
                    placeholder={field.placeholder}
                    value={numbers[field.key]}
                    onChange={(e) => setNumbers({ ...numbers, [field.key]: e.target.value })}
                  />
                </div>
              ))}
            </div>
            <fieldset className="space-y-2">
              <legend className="text-sm font-medium text-gray-800">Modes</legend>
              <div className="grid gap-2 sm:grid-cols-2">
                {CAN_MODES.map((mode) => (
                  <label key={mode} className="flex items-center gap-2 text-sm text-gray-700">
                    <input
                      type="checkbox"
                      className="h-4 w-4 rounded border-gray-300"
                      checked={modes[mode] ?? false}
                      onChange={(e) => setModes({ ...modes, [mode]: e.target.checked })}
                    />
                    {MODE_LABELS[mode]}
                  </label>
                ))}
              </div>
            </fieldset>
            <p className="text-xs text-gray-500">
              Bit timing, modes and restart-ms take the bus down for a moment while they are applied, for every client
              on it.
            </p>
            <DialogFooter>
              <Button type="button" variant="outline" onClick={onClose}>Cancel</Button>
              <Button type="submit" disabled={action.pending}>{action.pending ? 'Applying…' : 'Apply'}</Button>
            </DialogFooter>
          </form>
        )}
      </DialogContent>
    </Dialog>
  )
}

function textsOf(settings: InterfaceSettings | null): NumberTexts {
  const text = (value: number | undefined) => (value === undefined ? '' : String(value))

  return {
    bitrate: text(settings?.bitrate),
    samplePoint: text(settings?.samplePoint),
    dataBitrate: text(settings?.dataBitrate),
    dataSamplePoint: text(settings?.dataSamplePoint),
    restartMs: text(settings?.restartMs),
    txqueuelen: text(settings?.txqueuelen),
  }
}

// The change to send, or why the form cannot be applied.
function changeFrom(
  current: InterfaceSettings,
  numbers: NumberTexts,
  modes: Partial<Record<CanMode, boolean>>,
  up: boolean | undefined,
): InterfaceSettingsChange | string {
  const change: InterfaceSettingsChange = {}

  for (const field of NUMBER_FIELDS) {
    const text = numbers[field.key].trim()
    if (text === '' || Number(text) === current[field.key]) continue
    const value = Number(text)
    if (!Number.isFinite(value)) return `${field.label} must be a number`
    change[field.key] = value
  }

  const changedModes = CAN_MODES.filter((mode) => (modes[mode] ?? false) !== (current.modes[mode] ?? false))
  if (changedModes.length > 0) {
    change.modes = Object.fromEntries(changedModes.map((mode) => [mode, modes[mode] ?? false]))
  }
  if (up !== undefined && up !== current.up) change.up = up

  return Object.keys(change).length === 0 ? 'nothing changed' : change
}
