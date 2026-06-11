"""usage: python3 -m canhub fingerprint <certificate.pem>"""

import sys

from .fingerprint import identity_fingerprint


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] != "fingerprint":
        print(__doc__, file=sys.stderr)
        return 1

    print(identity_fingerprint(sys.argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
