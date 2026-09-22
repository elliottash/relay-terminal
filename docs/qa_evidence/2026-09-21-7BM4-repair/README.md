# #7BM4 — repair findings from fresh verification

Independent verifier a2 found that the real long card's Tests section was cut off by the
agent response limit, and that many Check findings collapsed into clipped rows. Implementation
repair d8b255157167a80c2693833019a8964bce6d0330 keeps the desktop card document complete while
retaining the agent's context limit, and puts findings in a bounded scrolling area.

Validation: 177 backend protocol tests pass, including a document with Tests after the 16 KiB
agent limit. BoardPane and CardTests pass 2/2, including a 30-finding case that checks readable
row heights and scroll access to the last row. See tests.txt. The exact proposed committed tree
passed the land.py application build gate.

The live drive uses that exact gate's GUI and matching backend, an isolated empty profile and
Xvfb, and a disposable card with a 30,000-character plan followed by 20 tests. It opens and reads
the tail, checks the actual Tests count and presses Check by stable name. Binary SHA256 and the
responses are in transcript.json; ui.png captures the scrollable findings. Rerun drive.py while
the recorded gate exists. This is implementer evidence, separate from a2's independent report.

The real-model Verify → Try it flow and the owner's unanswered judgement questions are separate
from these repairs. No end-to-end success is inferred from this fixture. Relay's board write
limit prevented recording the repair summary/status on #7BM4 during this turn; the commit and
this evidence are the durable repair record.
