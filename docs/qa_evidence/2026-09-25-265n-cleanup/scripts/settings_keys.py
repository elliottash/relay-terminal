#!/usr/bin/env python3
"""#265N audit (c): write-only / read-only QSettings key literals under src/.

Calls: setValue/value/remove/contains with a string-literal first argument
(plain or wrapped in QStringLiteral/QLatin1String) on receivers that look
like settings objects (settings, cfg, config, store, *Settings*, QSettings()).
QJsonObject::value() callers (obj.value("kind")) drop out by the receiver
filter. Same-basename matching flags keys read/written under a different
group prefix. Report-only.
"""
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/265n-head")
files = [p for p in (ROOT / "src").rglob("*.cpp")] + [p for p in (ROOT / "src").rglob("*.h")]

HELPERS = r"(?:setValue|value|remove|contains|stringValue|boolValue|intValue|doubleValue|bytesValue|listValue|jsonValue)"
CALL = re.compile(
    # QSettings-ish receivers: QSettings(), settings., m_settings->, relaySettings()., cfg->
    r'(?:QSettings\(\)|\b(?:[a-z]\w*(?:[Ss]ettings|Store|Config|Cfg)\w*|\bsettings\b|\bcfg\b|\bstore\b)\b(?:\s*\(\s*\))?)'
    r'\s*(?:->|\.)\s*(' + HELPERS + r')\s*\(\s*(?:QStringLiteral|QLatin1String|QLatin1Literal)?\s*\(\s*"([^"]+)"'
    r'|'
    # the SettingsCache helpers: relay::settings::stringValue("k")
    r'(?:relay::)?settings::(' + HELPERS + r')\s*\(\s*(?:QStringLiteral|QLatin1String|QLatin1Literal)?\s*\(\s*"([^"]+)"'
)

writes, reads, removes, contains = {}, {}, {}, {}
for p in files:
    try:
        body = p.read_text()
    except UnicodeDecodeError:
        continue
    for m in CALL.finditer(body):
        fn, key = (m.group(1), m.group(2)) if m.group(1) else (m.group(3), m.group(4))
        loc = f"{p.relative_to(ROOT)}:{body.count(chr(10), 0, m.start()) + 1}"
        bucket = writes if fn == "setValue" else reads if fn == "remove" and False else reads if fn in ("value", "stringValue", "boolValue", "intValue", "doubleValue", "bytesValue", "listValue", "jsonValue") else removes if fn == "remove" else contains
        bucket.setdefault(key, []).append(loc)

def base(k):
    return k.split("/")[-1]

print(f"key literals: {len(writes)} written, {len(reads)} read, {len(removes)} removed, {len(contains)} contains-checked")

wo = [k for k in writes if k not in reads and k not in removes and k not in contains]
print(f"\n== written, never read/removed/checked ({len(wo)}) ==")
for k in sorted(wo):
    print(f"  {k!r} at {', '.join(writes[k][:2])}  same-basename read: {any(base(k)==base(r) for r in reads)}")

ro = [k for k in reads if k not in writes and k not in contains]
print(f"\n== read, never written/checked ({len(ro)}) ==")
for k in sorted(ro):
    print(f"  {k!r} at {', '.join(reads[k][:2])}  same-basename written: {any(base(k)==base(w) for w in writes)}")
