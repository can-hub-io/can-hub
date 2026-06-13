// Small presentation helpers shared across the tab components.

export const peerId = (id: number) => `0x${id.toString(16)}`

export const shortFp = (fp: string) =>
  fp === '*' ? '* (any)' : fp.length > 16 ? fp.slice(0, 16) + '…' : fp

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
