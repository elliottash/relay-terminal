# SPDX-License-Identifier: GPL-3.0-or-later
"""The desktop's long-term identity and the devices it has paired with.

Two deliberate departures from ``backend/relay_core/keystore.py``, which stores API keys:

* **No environment override.** ``keystore.lookup`` honours ``RELAY_<ID>_API_KEY`` ahead of the
  keyring, which is right for a key you may want to inject for one run and wrong for an identity:
  anything that can set a variable in Relay's environment could otherwise substitute the key that
  every paired phone has pinned.
* **A missing key fails closed.** If the identity is gone, remote access is off until the user
  pairs again. It is never silently regenerated, because a new identity would make every pinned
  key mismatch and invite a "just trust the new one" prompt, which is the whole attack.

The key lives in the Secret Service keyring when one is available (``secret-tool``, the same
service name as API keys but a distinct attribute namespace), and otherwise in a 0600 file inside a
0700 directory, which is what a headless or minimal system gets.
"""
from __future__ import annotations

import json
import logging
import os
import shutil
import subprocess
import time
from dataclasses import dataclass, asdict, field
from pathlib import Path

from . import noise, pairing

log = logging.getLogger("relay.identity")

SERVICE = "org.relayterminal.Relay"
ATTRIBUTE = "remote-identity"          # distinct from the API-key namespace
TIMEOUT = 15


def state_dir() -> Path:
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    path = Path(base) / "relay" / "remote"
    path.mkdir(parents=True, exist_ok=True)
    path.chmod(0o700)
    return path


class Identity:
    """The desktop's static X25519 key.

    The storage is parameterised by three class attributes so another long-term key can reuse it
    without copying it: ``attribute`` is the keyring entry (a distinct namespace per key, so one
    can be regenerated without touching the other), ``filename`` the file it falls back to and
    ``label`` what the keyring shows. ``default_dir`` is where that file lives. Relay Free's
    installation key (``backend/relay_core/hosted.py``) subclasses this with its own values; the
    defaults here are the remote identity's, unchanged.
    """

    attribute = ATTRIBUTE
    filename = "identity.key"
    label = "Relay remote identity"

    @staticmethod
    def default_dir() -> Path:
        return state_dir()

    def __init__(self, private: bytes):
        self.private = private
        self.public = noise.public_of(private)

    @property
    def fingerprint(self) -> str:
        return pairing.fingerprint(self.public)

    @property
    def desktop_id(self) -> str:
        """Derived from the key, so a rendezvous cannot bind the id to a different one."""
        from hashlib import sha256
        return sha256(self.public).hexdigest()[:32]

    # ---- storage -----------------------------------------------------------------------------

    @staticmethod
    def _secret_tool() -> str | None:
        return shutil.which("secret-tool")

    @classmethod
    def _from_keyring(cls) -> bytes | None:
        tool = cls._secret_tool()
        if not tool or os.environ.get("RELAY_KEYRING") == "off":
            return None
        try:
            done = subprocess.run([tool, "lookup", "service", SERVICE, "key", cls.attribute],
                                  capture_output=True, timeout=TIMEOUT)
        except (OSError, subprocess.TimeoutExpired):
            return None
        if done.returncode != 0 or not done.stdout.strip():
            return None
        return pairing.un64(done.stdout.decode().strip())

    @classmethod
    def _to_keyring(cls, private: bytes) -> bool:
        tool = cls._secret_tool()
        if not tool or os.environ.get("RELAY_KEYRING") == "off":
            return False
        try:
            done = subprocess.run(
                [tool, "store", "--label=" + cls.label, "service", SERVICE, "key", cls.attribute],
                input=pairing.b64(private).encode(), capture_output=True, timeout=TIMEOUT)
        except (OSError, subprocess.TimeoutExpired):
            return False
        return done.returncode == 0

    @classmethod
    def load(cls, directory: Path | None = None) -> "Identity | None":
        """The existing identity, or None. Never generates one — see ``create``.

        The keyring holds the **profile's** identity and nothing else. An explicit ``directory``
        (the CLI's ``--state``, and every test's temporary directory) is a separate state with a
        separate identity, kept in its 0600 file. Before this rule a test that created an
        identity in a temp dir wrote it into the real keyring, over the owner's own key: every
        phone he had paired stopped matching the pinned key and had to be paired again.
        """
        private = cls._from_keyring() if directory is None else None
        if private and len(private) == 32:
            return cls(private)
        path = (directory or cls.default_dir()) / cls.filename
        if path.is_file():
            raw = path.read_text().strip()
            if raw:
                private = pairing.un64(raw)
                if len(private) == 32:
                    return cls(private)
        return None

    @classmethod
    def create(cls, directory: Path | None = None) -> "Identity":
        """Make a new identity. Only ever called when the user turns remote access on."""
        private, _ = noise.generate_keypair()
        if directory is not None or not cls._to_keyring(private):
            path = (directory or cls.default_dir()) / cls.filename
            path.write_text(pairing.b64(private))
            path.chmod(0o600)
        return cls(private)

    @classmethod
    def load_or_create(cls, directory: Path | None = None) -> "Identity":
        return cls.load(directory) or cls.create(directory)


# ---- devices ---------------------------------------------------------------------------------

@dataclass
class Device:
    device_id: str
    name: str
    platform: str
    public_key: str                    # base64url of the pinned static key
    capability: str = "view"
    created: float = field(default_factory=time.time)
    last_seen: float = 0.0
    password_entry: bool = False       # owner decision 5: off per device by default
    revoked: bool = False
    # The Web Push subscription (section 9): endpoint + content keys + the inner seal key, sent
    # by the phone inside the Noise session and kept only here — never at the rendezvous.
    push: dict | None = None
    # The rendezvous this device paired through, or last subscribed through: its pushes go out
    # there, signed with that server's VAPID key, because that is the key the browser subscribed
    # under. "" is the desktop's own local rendezvous, which is also what a record written before
    # this field existed means (the local server's port changes each run, so it is not stored).
    origin: str = ""

    @property
    def key_bytes(self) -> bytes:
        return pairing.un64(self.public_key)

    @property
    def fingerprint(self) -> str:
        return pairing.fingerprint(self.key_bytes)


def clean_label(text: str, limit: int = 40) -> str:
    """A device's own description of itself is attacker-controlled: strip it to bare text.

    It is shown in a dialog the user is about to click Allow on, so a name that can carry control
    characters, newlines or look-alike framing is a phishing tool. It never reaches a model prompt
    or a notification body either.
    """
    out = "".join(character for character in str(text)[:limit * 2]
                  if character.isprintable() and character not in "\r\n\t")
    return out.strip()[:limit] or "unnamed device"


class DeviceStore:
    """Paired devices, on disk next to the identity."""

    def __init__(self, directory: Path | None = None):
        self.directory = directory or state_dir()
        self.path = self.directory / "devices.json"
        self.devices: dict[str, Device] = {}
        self._on_revoke = []
        self.load()

    def on_revoke(self, callback) -> None:
        """Called with a device id the moment it is revoked or downgraded."""
        self._on_revoke.append(callback)

    def load(self) -> None:
        if not self.path.is_file():
            return
        try:
            raw = json.loads(self.path.read_text())
        except ValueError:
            return
        for entry in raw.get("devices", []):
            try:
                device = Device(**entry)
            except TypeError:
                continue
            self.devices[device.device_id] = device

    def save(self) -> None:
        payload = {"devices": [asdict(device) for device in self.devices.values()]}
        temporary = self.path.with_suffix(".tmp")
        temporary.write_text(json.dumps(payload, indent=2))
        temporary.chmod(0o600)
        os.replace(temporary, self.path)

    # ---- lookup ------------------------------------------------------------------------------

    def by_key(self, public_key: bytes) -> Device | None:
        """The live record for a static key. Revoked devices are not returned."""
        wanted = pairing.b64(public_key)
        for device in self.devices.values():
            if device.public_key == wanted and not device.revoked:
                return device
        return None

    def get(self, device_id: str) -> Device | None:
        device = self.devices.get(device_id)
        return device if device and not device.revoked else None

    def live(self) -> list[Device]:
        return [d for d in self.devices.values() if not d.revoked]

    # ---- changes -----------------------------------------------------------------------------

    def pair(self, public_key: bytes, name: str, platform: str, capability: str,
             origin: str = "") -> Device:
        import secrets
        existing = self.by_key(public_key)
        if existing is not None:                 # re-pairing the same key updates it in place
            existing.name = clean_label(name)
            existing.platform = clean_label(platform, 24)
            existing.capability = capability
            if existing.push is None:
                # A subscription it already holds was made under its old origin's key, and keeps
                # going out there until the phone subscribes again (which moves it).
                existing.origin = origin
            existing.last_seen = time.time()
            self.save()
            return existing
        device = Device(device_id=secrets.token_hex(8), name=clean_label(name),
                        platform=clean_label(platform, 24), public_key=pairing.b64(public_key),
                        capability=capability, origin=origin)
        self.devices[device.device_id] = device
        self.save()
        return device

    def set_capability(self, device_id: str, capability: str) -> bool:
        device = self.devices.get(device_id)
        if device is None:
            return False
        device.capability = capability
        self.save()
        self._notify(device_id)              # a downgrade must reach live sessions at once
        return True

    def set_push(self, device_id: str, subscription: dict | None,
                 origin: str | None = None) -> bool:
        """Store (or clear) a subscription; ``origin``, when given, is the rendezvous it was made
        under, and moves the device's pushes there."""
        device = self.devices.get(device_id)
        if device is None:
            return False
        device.push = subscription
        if origin is not None:
            device.origin = origin
        self.save()
        return True

    def set_password_entry(self, device_id: str, allowed: bool) -> bool:
        device = self.devices.get(device_id)
        if device is None:
            return False
        device.password_entry = allowed
        self.save()
        self._notify(device_id)
        return True

    def revoke(self, device_id: str) -> bool:
        device = self.devices.get(device_id)
        if device is None:
            return False
        device.revoked = True
        # A revoked device stops being notified at once: the subscription is the last thing that
        # could still reach a phone the owner has just taken off this desktop (section 9).
        device.push = None
        self.save()
        self._notify(device_id)
        return True

    def touch(self, device_id: str) -> None:
        device = self.devices.get(device_id)
        if device is not None:
            device.last_seen = time.time()

    def _notify(self, device_id: str) -> None:
        for callback in self._on_revoke:
            try:
                callback(device_id)
            except Exception:
                log.exception("revoke callback failed for device %s", device_id)
