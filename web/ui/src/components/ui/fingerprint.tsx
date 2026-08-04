import { useEffect, useState } from 'react'
import { Check, Copy } from 'lucide-react'
import { middleFp } from '../../lib'

const COPIED_MS = 1500

// navigator.clipboard is undefined outside a secure context, and the panel is
// commonly reached over plain http on a LAN, so fall back to a throwaway
// textarea + execCommand.
async function copy(text: string): Promise<boolean> {
  if (navigator.clipboard) {
    try {
      await navigator.clipboard.writeText(text)
      return true
    } catch {
      // fall through to the legacy path
    }
  }

  const scratch = document.createElement('textarea')
  scratch.value = text
  scratch.setAttribute('readonly', '')
  scratch.style.position = 'fixed'
  scratch.style.opacity = '0'
  document.body.appendChild(scratch)
  scratch.select()
  const copied = document.execCommand('copy')
  document.body.removeChild(scratch)
  return copied
}

export function Fingerprint({ value, full }: { value: string | null | undefined; full?: boolean }) {
  const [copied, setCopied] = useState(false)

  useEffect(() => {
    if (!copied) return
    const timer = setTimeout(() => setCopied(false), COPIED_MS)
    return () => clearTimeout(timer)
  }, [copied])

  if (!value) return <span className="text-gray-400">—</span>
  if (value === '*') return <span className="text-gray-500">* (any)</span>

  return (
    <span className="inline-flex items-center gap-1.5">
      <span className="font-mono text-xs" title={value}>{full ? value : middleFp(value)}</span>
      <button
        type="button"
        title={copied ? 'Copied' : 'Copy the full fingerprint'}
        aria-label={copied ? 'Copied' : 'Copy the full fingerprint'}
        className="rounded p-0.5 text-gray-400 transition-colors hover:bg-gray-100 hover:text-gray-700"
        onClick={() => copy(value).then(setCopied)}
      >
        {copied ? <Check size={13} className="text-primary-600" /> : <Copy size={13} />}
      </button>
    </span>
  )
}
