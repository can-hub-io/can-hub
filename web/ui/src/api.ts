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
  transport: string
  agentName: string
  origin: string
  fingerprintHex: string
  uptimeSeconds: number
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
  txDropped: number
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

export interface AuthState {
  needsBootstrap: boolean
  authenticated: boolean
  user: string | null
  permissions: string[]
  csrfToken: string | null
}

export interface AuditEntry {
  at: number
  actor: string
  action: string
  target: string
  status: number
}

// CSRF token from the current session, echoed on mutating requests.
let csrfToken: string | null = null

// Called when any request comes back 401, so the app can drop to the login
// screen (and stop the telemetry socket) the moment a session expires.
let unauthorizedHandler: (() => void) | null = null
export function setUnauthorizedHandler(handler: (() => void) | null) {
  unauthorizedHandler = handler
}

function noteStatus(status: number) {
  if (status === 401) unauthorizedHandler?.()
}

export interface ManagedUser {
  id: number
  name: string
  enabled: boolean
  groupIds: number[]
}

export interface ManagedGroup {
  id: number
  name: string
  permissions: string[]
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
    noteStatus(response.status)
    const detail = await response.json().catch(() => null)
    throw new Error(detail?.error ?? `request failed (${response.status})`)
  }
  return response.json() as Promise<T>
}

function mutatingHeaders(hasBody: boolean): Record<string, string> {
  const headers: Record<string, string> = {}
  if (hasBody) headers['content-type'] = 'application/json'
  if (csrfToken) headers['x-csrf-token'] = csrfToken
  return headers
}

async function send(path: string, method: string, body?: unknown): Promise<void> {
  const response = await fetch(path, {
    method,
    headers: mutatingHeaders(body !== undefined),
    body: body === undefined ? undefined : JSON.stringify(body),
  })
  if (!response.ok) {
    noteStatus(response.status)
    const detail = await response.json().catch(() => null)
    throw new Error(detail?.error ?? `request failed (${response.status})`)
  }
}

async function sendJson<T extends { csrfToken?: string | null }>(
  path: string,
  method: string,
  body: unknown,
): Promise<T> {
  const response = await fetch(path, {
    method,
    headers: mutatingHeaders(true),
    body: JSON.stringify(body),
  })
  if (!response.ok) {
    noteStatus(response.status)
    const detail = await response.json().catch(() => null)
    throw new Error(detail?.error ?? `request failed (${response.status})`)
  }
  const result = (await response.json()) as T
  if ('csrfToken' in result) csrfToken = result.csrfToken ?? null
  return result
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

  // auth
  authState: async () => {
    const state = await getJson<AuthState>('/api/auth/state')
    csrfToken = state.csrfToken
    return state
  },
  login: (name: string, password: string) => sendJson<AuthState>('/api/login', 'POST', { name, password }),
  setup: (name: string, password: string) => sendJson<AuthState>('/api/setup', 'POST', { name, password }),
  logout: async () => {
    await send('/api/logout', 'POST')
    csrfToken = null
  },
  changeOwnPassword: (currentPassword: string, newPassword: string) =>
    send('/api/auth/password', 'POST', { currentPassword, newPassword }),
  listAudit: () => getJson<AuditEntry[]>('/api/audit'),

  // user/group management
  listPermissions: () => getJson<string[]>('/api/permissions'),
  listManagedUsers: () => getJson<ManagedUser[]>('/api/users'),
  createManagedUser: (name: string, password: string) => send('/api/users', 'POST', { name, password }),
  deleteManagedUser: (id: number) => send(`/api/users/${id}`, 'DELETE'),
  setManagedUserEnabled: (id: number, enabled: boolean) => send(`/api/users/${id}/enabled`, 'POST', { enabled }),
  resetUserPassword: (id: number, password: string) => send(`/api/users/${id}/password`, 'POST', { password }),
  addMembership: (id: number, groupId: number) => send(`/api/users/${id}/groups`, 'POST', { groupId }),
  removeMembership: (id: number, groupId: number) => send(`/api/users/${id}/groups/${groupId}`, 'DELETE'),
  listManagedGroups: () => getJson<ManagedGroup[]>('/api/groups'),
  createManagedGroup: (name: string) => send('/api/groups', 'POST', { name }),
  deleteManagedGroup: (id: number) => send(`/api/groups/${id}`, 'DELETE'),
  setGroupPermissions: (id: number, permissions: string[]) =>
    send(`/api/groups/${id}/permissions`, 'PUT', { permissions }),
}

// Permission class required to see each tab.
export const PERMISSION = {
  viewsRead: 'views.read',
  peersKick: 'peers.kick',
  interfacesConfig: 'interfaces.config',
  pinsManage: 'pins.manage',
  aclManage: 'acl.manage',
  usersManage: 'users.manage',
} as const

export function telemetryUrl(): string {
  const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws'
  return `${scheme}://${window.location.host}/api/telemetry/ws`
}
