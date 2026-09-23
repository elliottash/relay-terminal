"""Run from the repository root with PYTHONPATH=backend:tests; no network requests."""
import tempfile
from unittest import mock

from relay_core import presets, session_protocol, guest_harness_provider as ghp
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig
from test_sessions import ScriptedProvider
from test_guest_harness_provider import WorkerProtocolTests
from guest_harness_fake import FakeHarness


def main():
    print("Guest regression, replaying the old reporting behavior:")
    case = WorkerProtocolTests()
    case.setUp()
    fixed_fields = ghp.configured_fields

    def old_fields(agent):
        fields = fixed_fields(agent)
        fields.pop("effort", None)
        return fields

    try:
        for guest, model in (("codex", "gpt-6-astra"), ("claude", "opus")):
            with mock.patch.object(ghp, "configured_fields", side_effect=old_fields):
                events = case.run_worker([
                    {"type": "configure", "preset": "guest:" + guest, "effort": "medium",
                     "workspace": ".", "guest": {"model": model, "effort": "medium"}},
                    {"type": "shutdown"}], FakeHarness([], guest=guest, model=model))
            configured = next(e for e in events if e["event"] == "configured")
            print(f"  {guest}/{model}: requested medium, harness {configured['guest_effort']}, "
                  f"old display {configured['effort']}")
    finally:
        case.doCleanups()

    print("API configuration audit, using built-in rows plus the locally cached catalog:")
    totals = [0, 0]
    with tempfile.TemporaryDirectory() as root:
        for pid, preset in presets.PRESETS.items():
            rows = presets.catalog_rows(pid)
            count = 0
            for row in rows:
                levels = row["efforts"]
                for level in levels or ["medium"]:
                    config = ProviderConfig(preset.base_url, row["id"], "test",
                                            presets.model_extra(pid, row["id"]))
                    agent = Agent(config, root, lambda e: None, provider=ScriptedProvider([]),
                                  preset_id=pid, effort=level, track_requests=False)
                    actual = session_protocol.configured_fields(agent)["effort"]
                    if levels:
                        assert actual == level, (pid, row["id"], actual, level)
                        assert presets.infer_effort(preset.effort_style, config.extra) == level
                    else:
                        assert "reasoning_effort" not in config.extra, (pid, row["id"])
                        assert "effort" not in config.extra.get("reasoning", {}), (pid, row["id"])
                    count += 1
            totals[0] += len(rows)
            totals[1] += count
            print(f"  {pid}: {len(rows)} models, {count} effort cases PASS")
    print(f"TOTAL: {totals[0]} API model entries; {totals[1]} effort cases PASS")
    print("Scope: configuration, event fields, and outgoing provider parameters; no live inference.")
    print("Unknown custom/local models without capability metadata are not exhaustively covered.")


if __name__ == "__main__":
    main()
