# Alternate-screen SSH smoke — pass

Latest build7 with final shell hooks. Real localhost SSH; printf enters the alternate screen, read waits for one character, and printf exits the alternate screen. The screenshot shows SCREEN-PROOF with the Take control affordance. Ctrl+H explicitly grants native input; q completes read. Ctrl+Shift+H returns the composer; AFTER-SCREEN runs with exit 0 and normal history/host context. Then exit returns locally. Initial harness attempts omitted native control or escaped the printf input incorrectly; final artifacts are from the corrected explicit-control run.

This tests terminal alternate-screen/input/restoration, not every full-screen application.
