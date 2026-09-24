# SPDX-License-Identifier: AGPL-3.0-or-later
"""One image, one call: POST the prompt to an upstream's ``/images`` endpoint.

The chat proxy streams; an image does not, so this is a plain request/response round trip
that the server runs in a thread off its event loop. It keeps the proxy's rules: the
operator key is read at call time, a redirect is refused (an ``Authorization`` header must
never follow a redirect to a host the operator did not configure), a transient refusal
(``proxy.RETRYABLE_STATUSES``) means the next upstream in the role is tried, and the
prompt is never logged.

The reply's body — an OpenRouter-images JSON carrying ``data[0].b64_json`` — is returned
to the client unchanged: the gateway never decodes or stores the picture.
"""
from __future__ import annotations

import json
import urllib.error
import urllib.request

from .config import Config, Role
from .proxy import NoRedirect, RETRYABLE_STATUSES, _socket_of, hard_close

# A 1024x1024 PNG is 1-3 MiB, so the base64 JSON around it is a few MiB. The desktop's
# inline-image writer already handles files of this size; the cap is for a runaway reply.
MAX_IMAGE_BODY = 32 * 1024 * 1024
USER_AGENT = "relay-gateway/0.1"


class ImageResult:
    """What the route needs: the winner's body, or the last failure."""

    def __init__(self):
        self.status = 0              # the upstream status that answered the client, 0 = none
        self.body: bytes | None = None
        self.provider = ""           # the upstream provider that served
        self.model = ""              # the model on that upstream
        self.attempts = 0            # upstreams tried; attempts - 1 were failovers
        self.error = ""              # a stable failure word, "" when a picture came back


def generate(config: Config, role: Role, prompt: str, resolution: str,
             aspect_ratio: str | None) -> ImageResult:
    """Blocking: try the role's upstreams in order until one returns an image."""
    result = ImageResult()
    for index, upstream in enumerate(role.upstreams):
        result.attempts = index + 1
        provider = config.provider_for(upstream)
        result.provider, result.model = provider.name, upstream.model
        payload = {"model": upstream.model, "prompt": prompt, "resolution": resolution}
        if aspect_ratio:
            payload["aspect_ratio"] = aspect_ratio
        if upstream.extra:
            payload.update(upstream.extra)
        request = urllib.request.Request(
            provider.base_url + "/images",
            data=json.dumps(payload, ensure_ascii=False).encode("utf-8"), method="POST",
            headers={"Content-Type": "application/json", "Accept": "application/json",
                     "User-Agent": USER_AGENT, "Authorization": "Bearer " + provider.key()})
        opener = urllib.request.build_opener(NoRedirect())
        try:
            response = opener.open(request, timeout=config.upstream_connect_timeout)
        except urllib.error.HTTPError as exc:
            # The body may echo the prompt; it is neither read nor logged.
            if getattr(exc, "fp", None) is not None:
                hard_close(exc.fp)
            result.status = exc.code
            if exc.code not in RETRYABLE_STATUSES:
                result.error = "upstream_refused"
                return result
            continue
        except (urllib.error.URLError, TimeoutError, OSError, ValueError):
            continue
        sock = _socket_of(response)
        if sock is not None:
            try:
                sock.settimeout(config.upstream_stall_timeout)
            except (OSError, ValueError):
                pass
        try:
            body = response.read(MAX_IMAGE_BODY + 1)
        except (TimeoutError, OSError, ValueError):
            continue
        finally:
            hard_close(response)
        if len(body) > MAX_IMAGE_BODY:
            result.status = 502
            continue
        result.status, result.body = 200, body
        return result
    result.error = result.error or "free_unavailable"
    return result
