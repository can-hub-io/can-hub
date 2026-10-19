import { useEffect, useState } from 'react'
import {
  api,
  CAN_MODES,
  type BitTiming,
  type CanMode,
  type InterfaceSettings,
  type InterfaceSettingsChange,
  type TimingMember,
} from '../api'
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

type NumberKey =
  | 'bitrate'
  | 'samplePoint'
  | 'dataBitrate'
  | 'dataSamplePoint'
  | 'restartMs'
  | 'txqueuelen'
  | 'termination'

const NUMBER_FIELDS: { key: NumberKey; label: string; placeholder: string }[] = [
  { key: 'bitrate', label: 'Bitrate (bit/s)', placeholder: '500000' },
  { key: 'samplePoint', label: 'Sample point', placeholder: '0.875' },
  { key: 'dataBitrate', label: 'Data bitrate (bit/s)', placeholder: '2000000' },
  { key: 'dataSamplePoint', label: 'Data sample point', placeholder: '0.75' },
  { key: 'restartMs', label: 'Restart after bus-off (ms)', placeholder: '0 = never' },
  { key: 'txqueuelen', label: 'TX queue length', placeholder: '10' },
  { key: 'termination', label: 'Termination (Ω)', placeholder: '0 = off' },
]

type Phase = 'bitTiming' | 'dataBitTiming'

// brp is only reported: the kernel derives it from tq.
type EditableMember = Exclude<TimingMember, 'brp'>

const PHASES: { key: Phase; label: string }[] = [
  { key: 'bitTiming', label: 'Nominal bit timing' },
  { key: 'dataBitTiming', label: 'Data bit timing (CAN FD)' },
]

const TIMING_FIELDS: { key: EditableMember; label: string }[] = [
  { key: 'tq', label: 'tq (ns)' },
  { key: 'propSeg', label: 'prop-seg' },
  { key: 'phaseSeg1', label: 'phase-seg1' },
  { key: 'phaseSeg2', label: 'phase-seg2' },
  { key: 'sjw', label: 'sjw' },
]

type TimingTexts = Record<EditableMember, string>

interface FormState {
  numbers: Record<NumberKey, string>
  timings: Record<Phase, TimingTexts>
  modes: Partial<Record<CanMode, boolean>>
  up: boolean | undefined
}

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
  const [form, setForm] = useState<FormState>(formOf(null))
  const name = `${agentName}/${interfaceName}`

  useEffect(() => {
    api.interfaceSettings(agentName, interfaceName)
      .then((settings) => {
        setCurrent(settings)
        setForm(formOf(settings))
      })
      .catch((cause) => setLoadError(cause instanceof Error ? cause.message : String(cause)))
  }, [agentName, interfaceName])

  const apply = (event: React.FormEvent) => {
    event.preventDefault()
    if (!current) return
    const change = changeFrom(current, form)
    if (typeof change === 'string') return action.setError(change)
    action.run(() => api.interfaceConfig(agentName, interfaceName, change), `${name} reconfigured`)
  }

  const setTiming = (phase: Phase, member: EditableMember, entered: string) =>
    setForm({ ...form, timings: { ...form.timings, [phase]: { ...form.timings[phase], [member]: entered } } })

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
                value={form.up === undefined ? '' : form.up ? 'up' : 'down'}
                onChange={(e) => setForm({ ...form, up: e.target.value === 'up' })}
              >
                {form.up === undefined && <option value="">unknown</option>}
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
                    value={form.numbers[field.key]}
                    onChange={(e) => setForm({ ...form, numbers: { ...form.numbers, [field.key]: e.target.value } })}
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
                      checked={form.modes[mode] ?? false}
                      onChange={(e) => setForm({ ...form, modes: { ...form.modes, [mode]: e.target.checked } })}
                    />
                    {MODE_LABELS[mode]}
                  </label>
                ))}
              </div>
            </fieldset>
            <details className="rounded-md border border-gray-200 px-3 py-2">
              <summary className="cursor-pointer text-sm font-medium text-gray-800">Advanced: raw bit timing</summary>
              <p className="mt-2 text-xs text-gray-500">
                Instead of a bitrate: the time quantum and the segment lengths in quanta. sjw alone may go with a
                bitrate.
              </p>
              {PHASES.map((phase) => (
                <fieldset className="mt-3 space-y-2" key={phase.key}>
                  <legend className="text-sm text-gray-700">
                    {phase.label}
                    {current[phase.key]?.brp !== undefined && (
                      <span className="ml-2 text-xs text-gray-500">brp {current[phase.key]?.brp}</span>
                    )}
                  </legend>
                  <div className="grid grid-cols-3 gap-2 sm:grid-cols-5">
                    {TIMING_FIELDS.map((field) => (
                      <div className="space-y-1" key={field.key}>
                        <label className="block text-xs text-gray-600" htmlFor={`configure-${phase.key}-${field.key}`}>
                          {field.label}
                        </label>
                        <Input
                          id={`configure-${phase.key}-${field.key}`}
                          inputMode="numeric"
                          value={form.timings[phase.key][field.key]}
                          onChange={(e) => setTiming(phase.key, field.key, e.target.value)}
                        />
                      </div>
                    ))}
                  </div>
                </fieldset>
              ))}
            </details>
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

function text(value: number | undefined): string {
  return value === undefined ? '' : String(value)
}

function timingTextsOf(timing: BitTiming | undefined): TimingTexts {
  return {
    tq: text(timing?.tq),
    propSeg: text(timing?.propSeg),
    phaseSeg1: text(timing?.phaseSeg1),
    phaseSeg2: text(timing?.phaseSeg2),
    sjw: text(timing?.sjw),
  }
}

function formOf(settings: InterfaceSettings | null): FormState {
  return {
    numbers: {
      bitrate: text(settings?.bitrate),
      samplePoint: text(settings?.samplePoint),
      dataBitrate: text(settings?.dataBitrate),
      dataSamplePoint: text(settings?.dataSamplePoint),
      restartMs: text(settings?.restartMs),
      txqueuelen: text(settings?.txqueuelen),
      termination: text(settings?.termination),
    },
    timings: {
      bitTiming: timingTextsOf(settings?.bitTiming),
      dataBitTiming: timingTextsOf(settings?.dataBitTiming),
    },
    modes: settings?.modes ?? {},
    up: settings?.up,
  }
}

// The members of one phase the user changed, or why they cannot be sent.
function timingChange(current: BitTiming | undefined, texts: TimingTexts): BitTiming | string | null {
  const change: BitTiming = {}

  for (const field of TIMING_FIELDS) {
    const entered = texts[field.key].trim()
    if (entered === '' || Number(entered) === current?.[field.key]) continue
    const value = Number(entered)
    if (!Number.isInteger(value) || value <= 0) return `${field.label} must be a positive whole number`
    change[field.key] = value
  }

  return Object.keys(change).length === 0 ? null : change
}

// The change to send, or why the form cannot be applied.
function changeFrom(current: InterfaceSettings, form: FormState): InterfaceSettingsChange | string {
  const change: InterfaceSettingsChange = {}

  for (const field of NUMBER_FIELDS) {
    const entered = form.numbers[field.key].trim()
    if (entered === '' || Number(entered) === current[field.key]) continue
    const value = Number(entered)
    if (!Number.isFinite(value)) return `${field.label} must be a number`
    change[field.key] = value
  }

  for (const phase of PHASES) {
    const timing = timingChange(current[phase.key], form.timings[phase.key])
    if (typeof timing === 'string') return timing
    if (timing) change[phase.key] = timing
  }

  const changedModes = CAN_MODES.filter((mode) => (form.modes[mode] ?? false) !== (current.modes[mode] ?? false))
  if (changedModes.length > 0) {
    change.modes = Object.fromEntries(changedModes.map((mode) => [mode, form.modes[mode] ?? false]))
  }
  if (form.up !== undefined && form.up !== current.up) change.up = form.up

  return Object.keys(change).length === 0 ? 'nothing changed' : change
}
