// Typed REST client mirroring the daemon DTOs (camelCase JSON).

export interface Status {
  peerCount: number
  agentCount: number
  clientCount: number
  interfaceCount: number
  framesReceived: number
  framesForwarded: number
  framesDropped: number
  framesUnroutable: number
}

export interface Peer {
  peerId: number
  role: string
  agentName: string
  fingerprintHex: string
  framesForwarded: number
  framesDropped: number
}

export interface Agent {
  peerId: number
  interfaceCount: number
  agentName: string
  fingerprintHex: string
}

export interface Client {
  peerId: number
  interfaceId: number
  channel: number | null
  agentName: string
  interfaceName: string
  framesForwarded: number
  framesDropped: number
}

export interface Interface {
  interfaceId: number
  subscriberCount: number
  framesReceived: number
  agentName: string
  interfaceName: string
}

export interface Acl {
  fingerprintHex: string
  agentName: string
  interfaceName: string
  level: string
}

export interface InterfaceRate {
  interfaceId: number
  agentName: string
  interfaceName: string
  framesReceived: number
  framesPerS: number
}

export interface TelemetryFrame {
  received: number
  forwarded: number
  dropped: number
  unroutable: number
  rates: {
    receivedPerS: number
    forwardedPerS: number
    droppedPerS: number
    unroutablePerS: number
  }
  interfaces: InterfaceRate[]
}

async function getJson<T>(path: string): Promise<T> {
  const response = await fetch(path)
  if (!response.ok) {
    const detail = await response.json().catch(() => null)
    throw new Error(detail?.error ?? `request failed (${response.status})`)
  }
  return response.json() as Promise<T>
}

export const api = {
  status: () => getJson<Status>('/api/status'),
  peers: () => getJson<Peer[]>('/api/peers'),
  agents: () => getJson<Agent[]>('/api/agents'),
  clients: () => getJson<Client[]>('/api/clients'),
  interfaces: () => getJson<Interface[]>('/api/interfaces'),
  acls: () => getJson<Acl[]>('/api/acls'),
}

export function telemetryUrl(): string {
  const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws'
  return `${scheme}://${window.location.host}/api/telemetry/ws`
}
