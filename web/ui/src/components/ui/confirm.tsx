import { useState, type ReactNode } from 'react'
import { Button, type ButtonProps } from './button'
import { Dialog, DialogContent, DialogFooter, DialogHeader, DialogTitle } from './dialog'

export function ConfirmButton({
  title,
  description,
  confirmLabel,
  destructive,
  onConfirm,
  children,
  ...props
}: Omit<ButtonProps, 'onClick'> & {
  title: string
  description?: ReactNode
  confirmLabel: string
  destructive?: boolean
  onConfirm: () => void
  children: ReactNode
}) {
  const [open, setOpen] = useState(false)

  const confirm = () => {
    setOpen(false)
    onConfirm()
  }

  return (
    <>
      <Button {...props} onClick={() => setOpen(true)}>{children}</Button>
      {open && (
        <Dialog open onOpenChange={setOpen}>
          <DialogContent>
            <DialogHeader><DialogTitle>{title}</DialogTitle></DialogHeader>
            {description && <p className="text-sm text-gray-600">{description}</p>}
            <DialogFooter>
              <Button variant="outline" onClick={() => setOpen(false)}>Cancel</Button>
              <Button variant={destructive ? 'destructive' : 'default'} onClick={confirm}>{confirmLabel}</Button>
            </DialogFooter>
          </DialogContent>
        </Dialog>
      )}
    </>
  )
}
