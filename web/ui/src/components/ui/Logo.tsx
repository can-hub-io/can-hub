export function Logo({ size = 32, className }: { size?: number; className?: string }) {
  return (
    <svg
      xmlns="http://www.w3.org/2000/svg"
      viewBox="0 0 100 100"
      width={size}
      height={size}
      role="img"
      aria-label="can-hub"
      className={className}
    >
      <polygon
        points="50.00,10.00 84.64,30.00 84.64,70.00 50.00,90.00 15.36,70.00 15.36,30.00"
        fill="#ffffff"
        stroke="#0b6e3a"
        strokeWidth="4.2"
        strokeLinejoin="round"
      />
      <polygon
        points="50.00,26.80 70.09,38.40 70.09,61.60 50.00,73.20 29.91,61.60 29.91,38.40"
        fill="#0b6e3a"
      />
      <circle cx="50" cy="50" r="6.40" fill="#ffffff" />
    </svg>
  )
}
