# SPDX-License-Identifier: GPL-3.0-or-later
"""A self-signed certificate for development, so a phone can reach the app over https.

WebCrypto is only available in a **secure context**, and a phone on a LAN or tailnet address is not
one over plain http. There are two honest ways out:

* ``tailscale serve`` gives a real certificate for ``<machine>.<tailnet>.ts.net`` and no warning,
  but it needs ``sudo tailscale set --operator=$USER`` once on this machine;
* this, which needs nothing, but the phone shows a certificate warning the first time.

Neither weakens RRP itself: the Noise session inside is authenticated by the key the phone pinned
from the QR code, so TLS here is only what the browser demands before it will hand out WebCrypto.
"""
from __future__ import annotations

import datetime
import ipaddress
import socket
import ssl
import subprocess
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

VALID_DAYS = 365


def local_addresses() -> list[str]:
    """Every address this machine might be reached on, most-likely-to-work first.

    Ordinary network addresses come before tailnet ones. A phone on the same Wi-Fi is the common
    case and reaches the LAN address; a tailnet address only works if the phone itself is signed
    in to the tailnet and online, which is easy to assume and wrong. The caller offers the list so
    the person can pick the other one when the first does not answer.
    """
    lan: list[str] = []
    tailnet: list[str] = []
    try:
        output = subprocess.run(["ip", "-4", "-o", "addr", "show", "scope", "global"],
                                capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        output = ""
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        interface, address = parts[1], parts[3].split("/")[0]
        if address.startswith("172.17.") or interface.startswith("docker"):
            continue
        (tailnet if interface.startswith("tailscale") else lan).append(address)
    return lan + tailnet


def describe(address: str) -> str:
    """A word for where an address can be reached from, for the picker."""
    return "tailnet" if address.startswith("100.") else "this network"


def preferred_address() -> str:
    addresses = local_addresses()
    return addresses[0] if addresses else "127.0.0.1"


def ensure_cert(directory: Path) -> tuple[Path, Path]:
    """A certificate covering this machine's addresses, made once and reused."""
    directory.mkdir(parents=True, exist_ok=True)
    cert_path = directory / "dev-cert.pem"
    key_path = directory / "dev-key.pem"
    names = sorted(set(local_addresses()) | {"127.0.0.1"})
    if cert_path.is_file() and key_path.is_file():
        try:
            existing = x509.load_pem_x509_certificate(cert_path.read_bytes())
            covered = {entry.value.exploded for entry
                       in existing.extensions.get_extension_for_class(
                           x509.SubjectAlternativeName).value.get_values_for_type(x509.IPAddress)}
            fresh = existing.not_valid_after_utc > datetime.datetime.now(datetime.timezone.utc)
            if fresh and set(names).issubset(covered):
                return cert_path, key_path
        except Exception:
            pass                                    # regenerate rather than puzzle over it

    key = ec.generate_private_key(ec.SECP256R1())
    hostname = socket.gethostname()
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, hostname[:60] or "relay")])
    alternatives = [x509.IPAddress(ipaddress.ip_address(name)) for name in names]
    alternatives.append(x509.DNSName("localhost"))
    if hostname:
        alternatives.append(x509.DNSName(hostname))
    now = datetime.datetime.now(datetime.timezone.utc)
    certificate = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=5))
        .not_valid_after(now + datetime.timedelta(days=VALID_DAYS))
        .add_extension(x509.SubjectAlternativeName(alternatives), critical=False)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .sign(key, hashes.SHA256()))

    cert_path.write_bytes(certificate.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                                           serialization.PrivateFormat.PKCS8,
                                           serialization.NoEncryption()))
    key_path.chmod(0o600)
    return cert_path, key_path


def context(directory: Path) -> ssl.SSLContext:
    cert_path, key_path = ensure_cert(directory)
    ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ssl_context.load_cert_chain(cert_path, key_path)
    return ssl_context


def fingerprint(directory: Path) -> str:
    """The certificate's SHA-256, so the warning on the phone can be checked against something."""
    cert_path, _ = ensure_cert(directory)
    certificate = x509.load_pem_x509_certificate(cert_path.read_bytes())
    digest = certificate.fingerprint(hashes.SHA256()).hex().upper()
    return " ".join(digest[i:i + 4] for i in range(0, 16, 4))
