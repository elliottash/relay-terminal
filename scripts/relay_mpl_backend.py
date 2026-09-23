"""Matplotlib pyplot backend that displays figures in a Relay terminal pane.

Relay sets MPLBACKEND=module://relay_mpl_backend for new local pane shells when
the user has not selected another backend. Saved PNGs are content-addressed so
the image rows can be restored after a Relay restart.
"""

import base64
import hashlib
import io
import os
from pathlib import Path
import sys

from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.backend_bases import FigureManagerBase


class FigureManagerRelay(FigureManagerBase):
    def show(self):
        data = io.BytesIO()
        self.canvas.print_png(data)
        raw = data.getvalue()
        root = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "relay" / "media"
        root.mkdir(parents=True, exist_ok=True)
        root.chmod(0o700)
        path = root / (hashlib.sha256(raw).hexdigest() + ".png")
        if not path.exists():
            path.write_bytes(raw)
            path.chmod(0o600)
        payload = base64.b64encode(os.fsencode(path))
        sys.stdout.buffer.write(b"\x1b_Ga=T,t=f,f=100,q=2;" + payload + b"\x1b\\\n")
        sys.stdout.buffer.flush()


class FigureCanvas(FigureCanvasAgg):
    manager_class = FigureManagerRelay
