// Small presentation helpers shared across the tab components.

import { type Interface } from './api'

export const selectClass =
  'h-9 rounded-md border border-gray-300 bg-white px-3 text-sm text-gray-900 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary-500/40'

// True when the pair names an interface no live agent is exporting, which is
// either a typo or a deliberate pre-authorisation.
export function unknownObject(interfaces: Interface[] | null, agent: string, iface: string): boolean {
  if (!interfaces || agent === '*' || iface === '*' || !agent || !iface) return false

  return !interfaces.some((i) => i.agentName === agent && i.interfaceName === iface)
}

export const peerId = (id: number) => `0x${id.toString(16)}`

// Fingerprints are compared by eye from both ends, so keep the head and the
// tail rather than the first half.
export const middleFp = (fp: string) =>
  fp === '*' ? '* (any)' : fp.length > 20 ? `${fp.slice(0, 8)}…${fp.slice(-8)}` : fp

// The hub encodes the transport in the top two bits of the peer id (see
// hub_main.c peer_id ranges): tcp, unix, quic, tls.
export function transportOf(id: number): string {
  switch ((id >>> 30) & 0x3) {
    case 0:
      return 'tcp'
    case 1:
      return 'unix'
    case 2:
      return 'quic'
    default:
      return 'tls'
  }
}

export function formatUptime(seconds: number): string {
  if (seconds < 60) return `${seconds}s`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m${seconds % 60}s`
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h${Math.floor((seconds % 3600) / 60)}m`
  return `${Math.floor(seconds / 86400)}d${Math.floor((seconds % 86400) / 3600)}h`
}

export const can = (permissions: string[], permission: string) => permissions.includes(permission)
