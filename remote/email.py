# SPDX-License-Identifier: GPL-3.0-or-later
"""Send an invite link by email, through AWS SES.

An invite link is a secret (section 10.2): 128 bits in the fragment, one use over a public link,
and the person still has to knock and be admitted by hand. Handing it over is the owner's problem
— a chat message, a QR code held up to a camera, or this. Email is the one that works for a
colleague who is not in the room, so it is here.

What this is not: it is **not** a second way in. The link that goes in the mail is the link the
dialog already minted, with the same expiry, the same single use and the same knock at the end. An
email that goes astray is an invite that goes astray, which is why the owner chooses the address
and why nothing here can be triggered from the wire — `invite_email` is desktop-only, like every
other name in `wire.OWNER_ONLY`.

Credentials never reach this repository or a log. They are read, in order, from:

* the process environment (`AWS_SES_ACCESS_KEY_ID`, `AWS_SES_SECRET_ACCESS_KEY`), which is how the
  owner's other project does it;
* a named AWS profile (`RELAY_AWS_PROFILE`, or `eth-luth-admin` when that exists), i.e. the
  ordinary `~/.aws/credentials`;
* the default boto3 chain, so an instance role or `AWS_PROFILE` works too.

`RELAY_MAIL_FROM` is the sender, and the region is `RELAY_AWS_REGION` or the profile's own. Nothing
is sent unless a sender is configured: an invite that arrives from an unverified address is worse
than one the owner pastes into a chat window himself.
"""
from __future__ import annotations

import os
import re
from dataclasses import dataclass

DEFAULT_PROFILE = "eth-luth-admin"

# Deliberately loose: the address is typed by the owner, and the check exists to catch a slip
# before an invite is spent, not to be a specification.
ADDRESS = re.compile(r"^[^@\s,;]+@[^@\s,;]+\.[a-z]{2,}$", re.IGNORECASE)


@dataclass
class Mailer:
    """What is configured, or the sentence saying what is missing."""

    sender: str = ""
    region: str = ""
    profile: str = ""
    reason: str = ""

    @property
    def ready(self) -> bool:
        return bool(self.sender) and not self.reason


def configured() -> Mailer:
    """Whether an invite can be emailed from this machine, and if not, why not."""
    try:
        import boto3  # noqa: F401
    except ImportError:
        return Mailer(reason="boto3 is not installed here, so Relay cannot reach SES. "
                             "Copy the link instead, or `pip install boto3`.")
    sender = (os.environ.get("RELAY_MAIL_FROM") or "").strip()
    if not sender:
        return Mailer(reason="No sender address: set RELAY_MAIL_FROM to an address SES has "
                             "verified, then try again.")
    region = (os.environ.get("RELAY_AWS_REGION") or os.environ.get("AWS_SES_REGION") or "").strip()
    profile = (os.environ.get("RELAY_AWS_PROFILE") or "").strip()
    if not profile and not os.environ.get("AWS_SES_ACCESS_KEY_ID"):
        profile = DEFAULT_PROFILE if _has_profile(DEFAULT_PROFILE) else ""
    return Mailer(sender=sender, region=region, profile=profile)


def _has_profile(name: str) -> bool:
    try:
        import boto3

        return name in boto3.Session().available_profiles
    except Exception:
        return False


def _client(mailer: Mailer):
    import boto3

    key_id = os.environ.get("AWS_SES_ACCESS_KEY_ID")
    secret = os.environ.get("AWS_SES_SECRET_ACCESS_KEY")
    if key_id and secret:
        session = boto3.Session(aws_access_key_id=key_id, aws_secret_access_key=secret)
    elif mailer.profile:
        session = boto3.Session(profile_name=mailer.profile)
    else:
        session = boto3.Session()
    return session.client("sesv2", region_name=mailer.region or session.region_name)


def body(url: str, *, role_sentence: str, expiry: str, pane: str, sender_name: str) -> tuple[str, str]:
    """The subject and the text of the invitation. Plain text: a link in a styled HTML mail from a
    machine is the shape of every phishing mail ever sent, and this one asks somebody to click."""
    what = pane.strip() or "a terminal pane"
    subject = f"{sender_name} shared a terminal with you" if sender_name else "A terminal has been shared with you"
    text = (
        f"{sender_name or 'Somebody'} is sharing {what} with you in Relay, a terminal.\n\n"
        f"Open this link to ask to join:\n\n  {url}\n\n"
        f"{role_sentence} The link works once and {expiry}; after you open it, "
        f"{sender_name or 'they'} still has to let you in, and can end the session at any time.\n\n"
        "You do not need an account, and nothing is installed: it opens in your browser.\n"
    )
    return subject, text


def send(to: str, url: str, *, role_sentence: str, expiry: str, pane: str,
         sender_name: str = "") -> tuple[bool, str]:
    """Send one invitation. Returns (sent, a sentence for the dialog).

    Never raises and never logs the link: a failure here is a message on screen, and the owner
    still has the link in front of him to send another way.
    """
    address = (to or "").strip()
    if not ADDRESS.match(address):
        return False, f"{address or 'That'} does not look like an email address."
    mailer = configured()
    if not mailer.ready:
        return False, mailer.reason
    subject, text = body(url, role_sentence=role_sentence, expiry=expiry, pane=pane,
                         sender_name=sender_name)
    try:
        _client(mailer).send_email(
            FromEmailAddress=mailer.sender,
            Destination={"ToAddresses": [address]},
            Content={"Simple": {"Subject": {"Data": subject, "Charset": "UTF-8"},
                                "Body": {"Text": {"Data": text, "Charset": "UTF-8"}}}})
    except Exception as error:                      # noqa: BLE001 - every failure is a sentence
        return False, _explain(error, mailer, address)
    return True, f"Invitation sent to {address}."


def _explain(error: Exception, mailer: Mailer, address: str) -> str:
    """AWS's own words are unreadable on a dialog; these are the three that actually happen."""
    name = type(error).__name__
    text = str(error)
    if "MessageRejected" in name or "not verified" in text:
        # The sandbox is the usual one: until production access is granted, SES will only send to
        # addresses it has verified, and only from one.
        return (f"SES refused it: {mailer.sender} or {address} is not verified. A new SES account "
                f"is in the sandbox — verify both addresses, or ask AWS for production access to "
                f"email anyone.")
    if "AccessDenied" in name or "AccessDenied" in text or "not authorized" in text:
        return "Those AWS credentials may not send mail through SES (ses:SendEmail is missing)."
    if "NoCredentials" in name or "Unable to locate credentials" in text:
        return ("No AWS credentials for SES: set AWS_SES_ACCESS_KEY_ID and "
                "AWS_SES_SECRET_ACCESS_KEY, or RELAY_AWS_PROFILE.")
    return f"SES would not send it ({name}). The link is still on screen to send another way."
