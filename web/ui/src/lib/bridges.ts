import { type BridgeEndpoint, type CanFilter } from '../api'

const EXACT_MASK = 0x1fffffff
const FILTERS_MAX = 16

export function nameOf(endpoint: BridgeEndpoint): string {
  return `${endpoint.agentName}/${endpoint.interfaceName}`
}

export function endpointOf(text: string): BridgeEndpoint | null {
  const [agentName, interfaceName, ...rest] = text.trim().split('/')
  if (!agentName || !interfaceName || rest.length > 0) return null
  return { agentName, interfaceName }
}

// "100 200:700" → exact id 0x100 and the ids 0x200 matches under mask 0x700.
export function filtersOf(text: string): CanFilter[] | string {
  const words = text.trim().split(/[\s,]+/).filter(Boolean)
  if (words.length > FILTERS_MAX) return `At most ${FILTERS_MAX} ids`
  const filters: CanFilter[] = []
  for (const word of words) {
    const [id, mask, ...rest] = word.split(':')
    const canId = parseInt(id, 16)
    const canMask = mask === undefined ? EXACT_MASK : parseInt(mask, 16)
    if (rest.length > 0 || !/^[0-9a-f]+$/i.test(id) || Number.isNaN(canMask) || (mask !== undefined && !/^[0-9a-f]+$/i.test(mask))) {
      return `${word} is not an id or id:mask in hex`
    }
    filters.push({ canId, canMask })
  }
  return filters
}

export function textOf(filters: CanFilter[]): string {
  if (filters.length === 0) return 'all'
  return filters
    .map((f) => (f.canMask === EXACT_MASK ? hex(f.canId) : `${hex(f.canId)}:${hex(f.canMask)}`))
    .join(' ')
}

function hex(value: number): string {
  return value.toString(16).toUpperCase()
}
