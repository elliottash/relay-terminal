# #KKYC evidence — folder click in terminal output

Commit c43874b9. Build: `scripts/relay-build --check "Navigate here<TAB>Shift+click"` passed (string present in build/relay); land.py built the exact tree in a verify slot.

```
PASS   : BackendsTests::initTestCase()
PASS   : BackendsTests::coreSelection()
PASS   : BackendsTests::processDefaultCoreIsRemembered()
PASS   : BackendsTests::menuKeepsRelayOwnEntriesFirst()
PASS   : BackendsTests::menuCarriesTheTerminalItemsWorthKeeping()
PASS   : BackendsTests::menuCarriesEqualizeGreyedWhileThePaneIsAlone()
PASS   : BackendsTests::menuIsTheSameOnAnyBackendExceptForCapabilities()
PASS   : BackendsTests::menuGreysCopyOnlyWhenTheEngineKnowsTheSelection()
PASS   : BackendsTests::menuOffersLinkAndFileEntriesOnlyWhenThereIsOneUnderThePointer()
PASS   : BackendsTests::menuOffersNavigateHereOnlyOnAFolder()
PASS   : BackendsTests::folderClickFollowsTheModifiers()
PASS   : BackendsTests::folderClickMenuNamesBothChoicesAndTheirChords()
PASS   : BackendsTests::menuOffersANewPaneOnTheSameHostOnlyInARemotePane()
PASS   : BackendsTests::menuOffersACardReferenceUnderThePointer()
PASS   : BackendsTests::menuNeverHasALooseSeparator()
PASS   : BackendsTests::cleanupTestCase()
Totals: 16 passed, 0 failed, 0 skipped, 0 blacklisted, 1ms
```

Not done here: a person clicking a folder in `ls` output (menu at the pointer; Ctrl+click → explorer; Shift+click → cd; Shift+click while vim runs → refused with a status, nothing typed).
