import { cva, type VariantProps } from 'class-variance-authority'
import { type HTMLAttributes } from 'react'
import { cn } from '../../lib/utils'

const badgeVariants = cva(
  'inline-flex items-center rounded-full border px-2.5 py-0.5 text-xs font-semibold',
  {
    variants: {
      variant: {
        default: 'border-transparent bg-gray-900 text-white',
        secondary: 'border-transparent bg-gray-100 text-gray-700',
        success: 'border-transparent bg-green-100 text-green-700',
        destructive: 'border-transparent bg-red-100 text-red-700',
        warning: 'border-transparent bg-amber-100 text-amber-700',
        primary: 'border-transparent bg-primary-100 text-primary-700',
        outline: 'text-gray-700 border-gray-300',
      },
    },
    defaultVariants: { variant: 'secondary' },
  },
)

export interface BadgeProps extends HTMLAttributes<HTMLSpanElement>, VariantProps<typeof badgeVariants> {}

export function Badge({ className, variant, ...props }: BadgeProps) {
  return <span className={cn(badgeVariants({ variant }), className)} {...props} />
}
