# Alt+I toggle verification

- `scripts/relay-build --target relay`: passed.
- `python3 docs/qa_evidence/2026-09-21-alt-i-toggle/drive.py`: passed using the real GUI under Xvfb, isolated XDG directories, and a fake stdio worker (no model calls).
- Actual keypresses verified: Alt+I opens info; second Alt+I closes; typing appears in the owner composer; third Alt+I reopens; Escape closes; another open/close cycle succeeds.
- Screenshot OCR asserts the info footer is visible only in the open states and the typed focus marker appears after close. The reproducible driver saves its screenshots and OCR beside itself.
- Initial fixture attempts did not configure an agent before opening info; the fixture now sends a no-op agent prompt first. No application change was needed for that prerequisite.
- Existing mouse shortcut hint continues to use the live agent.info binding.
