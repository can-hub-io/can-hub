/* eslint-disable react-refresh/only-export-components */
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import { Check, X } from 'lucide-react'
import { cn } from '../../lib/utils'

const TOAST_MS = 3000

interface Toast {
  id: number
  message: string
  failed: boolean
}

const ToastContext = createContext<(message: string, failed?: boolean) => void>(() => {})

export function useToast() {
  return useContext(ToastContext)
}

export function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<Toast[]>([])
  const nextId = useRef(1)

  const push = useCallback((message: string, failed = false) => {
    const id = nextId.current++
    setToasts((current) => [...current, { id, message, failed }])
  }, [])

  const dismiss = useCallback((id: number) => {
    setToasts((current) => current.filter((toast) => toast.id !== id))
  }, [])

  const value = useMemo(() => push, [push])

  return (
    <ToastContext.Provider value={value}>
      {children}
      <div className="pointer-events-none fixed bottom-4 right-4 z-[60] flex flex-col gap-2">
        {toasts.map((toast) => (
          <ToastItem key={toast.id} toast={toast} onDismiss={dismiss} />
        ))}
      </div>
    </ToastContext.Provider>
  )
}

function ToastItem({ toast, onDismiss }: { toast: Toast; onDismiss: (id: number) => void }) {
  useEffect(() => {
    const timer = setTimeout(() => onDismiss(toast.id), TOAST_MS)
    return () => clearTimeout(timer)
  }, [toast.id, onDismiss])

  return (
    <div
      role="status"
      className={cn(
        'pointer-events-auto flex max-w-sm items-start gap-2 rounded-lg border px-3 py-2 text-sm shadow-lg',
        toast.failed ? 'border-red-200 bg-red-50 text-red-800' : 'border-gray-200 bg-white text-gray-800',
      )}
    >
      {toast.failed ? <X size={16} className="mt-0.5 shrink-0 text-red-600" /> : <Check size={16} className="mt-0.5 shrink-0 text-primary-600" />}
      <span>{toast.message}</span>
    </div>
  )
}
