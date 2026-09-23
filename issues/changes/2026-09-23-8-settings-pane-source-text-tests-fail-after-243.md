---
id: 15ZE
type: work
status: inbox
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: '#HJ1T session, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [243T], github: null}
---
# 8 settings-pane source-text tests fail after #243T moved window bodies into .cpp files

## Issue
(found by the #HJ1T session, not a user request) `xvfb-run -a build/relay-settings-tests -silent` on main at d6f769f6: 40 passed, 8 failed. All 8 read src/RelayWindow.h as text for bodies that f6480574 (#243T) moved to .cpp files: everyRowHelperDeclaresADefault, workSignalsUnaskedSitsUnderBoardAndSaysWhatItDoes, theModelsPageHasNoTierListsAndNoChecklist ("modelsSection() is gone"), theModelsPageButtonOpensTheModelsPaneOnMain, everyProviderRowHasAModelsLinkIntoTheAvailableTab, everyProviderButTheFirstDrawsARuleAboveIt, theAdvancedProviderDialogIsGoneAndItsTwoPartsHaveHomes, localModelsComeRightAfterModels.
