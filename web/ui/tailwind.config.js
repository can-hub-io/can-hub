/** @type {import('tailwindcss').Config} */
export default {
  darkMode: 'class',
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        primary: {
          50: '#e7f4ec',
          100: '#d8f0e4',
          200: '#b3e3c9',
          300: '#7fd0a8',
          400: '#43b681',
          500: '#149a5e',
          600: '#0b6e3a',
          700: '#0a5e32',
          800: '#094d2a',
          900: '#073d22',
        },
        teal: {
          500: '#0f9d8f',
        },
      },
      fontFamily: {
        mono: ['ui-monospace', 'SFMono-Regular', 'Menlo', 'Consolas', 'monospace'],
      },
    },
  },
  plugins: [],
}
