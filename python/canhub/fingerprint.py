"""can-hub fingerprints without native code: sha256 over the certificate DER."""

import base64
import hashlib
import re

_PEM_BODY = re.compile(
    r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----",
    re.DOTALL,
)


def identity_fingerprint(certificate_path: str) -> str:
    """Fingerprint of a PEM certificate: what the hub pins and the ACLs key on."""
    with open(certificate_path, "r", encoding="ascii") as certificate_file:
        pem = certificate_file.read()

    match = _PEM_BODY.search(pem)
    if match is None:
        raise ValueError(f"{certificate_path} does not contain a PEM certificate")

    der = base64.b64decode(match.group(1))
    return hashlib.sha256(der).hexdigest()
