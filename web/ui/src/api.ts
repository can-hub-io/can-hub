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

export interface Pin {
  agentName: string
  fingerprintHex: string
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

async function send(path: string, method: string, body?: unknown): Promise<void> {
  const response = await fetch(path, {
    method,
    headers: body === undefined ? undefined : { 'content-type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  })
  if (!response.ok) {
    const detail = await response.json().catch(() => null)
    throw new Error(detail?.error ?? `request failed (${response.status})`)
  }
}

export type AclLevel = 'none' | 'ro' | 'rw'
export type IfconfigOp = 'bitrate' | 'up' | 'down'

export const api = {
  status: () => getJson<Status>('/api/status'),
  peers: () => getJson<Peer[]>('/api/peers'),
  agents: () => getJson<Agent[]>('/api/agents'),
  clients: () => getJson<Client[]>('/api/clients'),
  interfaces: () => getJson<Interface[]>('/api/interfaces'),
  acls: () => getJson<Acl[]>('/api/acls'),
  pins: () => getJson<Pin[]>('/api/pins'),

  kickPeer: (peerId: number) => send(`/api/peers/${peerId}/kick`, 'POST'),
  kickAgent: (name: string) => send(`/api/agents/${encodeURIComponent(name)}/kick`, 'POST'),
  pinAdd: (agentName: string, fingerprintHex: string) =>
    send('/api/pins', 'POST', { agentName, fingerprintHex }),
  pinDelete: (name: string) => send(`/api/pins/${encodeURIComponent(name)}`, 'DELETE'),
  aclSet: (fingerprintHex: string, agentName: string, interfaceName: string, level: AclLevel) =>
    send('/api/acls', 'POST', { fingerprintHex, agentName, interfaceName, level }),
  aclRevoke: (fingerprintHex: string, agentName: string, interfaceName: string) =>
    send('/api/acls/revoke', 'POST', { fingerprintHex, agentName, interfaceName }),
  interfaceConfig: (agentName: string, interfaceName: string, op: IfconfigOp, bitrate: number) =>
    send('/api/interfaces/config', 'POST', { agentName, interfaceName, op, bitrate }),
}

export function telemetryUrl(): string {
  const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws'
  return `${scheme}://${window.location.host}/api/telemetry/ws`
}
