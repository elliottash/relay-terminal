# Usage limits in the picker; an exhausted subscription is greyed and skipped (#XH4K)

Session exhausted-gui, 2026-09-20. Test output as run from build/ on this tree.

```
$ QT_QPA_PLATFORM=offscreen ./relay-modelcatalog-tests
PASS   : ModelCatalogTests::initTestCase()
PASS   : ModelCatalogTests::everyModelOfEveryPresetIsAnEntry()
PASS   : ModelCatalogTests::keysSurviveColonsAndSlashes()
PASS   : ModelCatalogTests::shownDefaultsToEveryUsableEntry()
PASS   : ModelCatalogTests::unCheckingOneMaterialisesTheListAndHidesOnlyThat()
PASS   : ModelCatalogTests::defaultRankPutsTheDefaultProviderFirst()
PASS   : ModelCatalogTests::movingARankPersistsAndDroppedKeysAreForgotten()
PASS   : ModelCatalogTests::aCustomModelIsAnEntryOfItsProviderAndShownAtOnce()
PASS   : ModelCatalogTests::sortsAreStableOverRank()
PASS   : ModelCatalogTests::recentIsCappedAtTen()
PASS   : ModelCatalogTests::effortIsRememberedPerEntry()
PASS   : ModelCatalogTests::openrouterFallbackIsPerModelAndOffByDefault()
PASS   : ModelCatalogTests::collapsedProvidersAndTheFallbackThreshold()
PASS   : ModelCatalogTests::providerOrderIsAdditionThenDrag()
PASS   : ModelCatalogTests::sortRoundTrips()
PASS   : ModelCatalogTests::limitsTextNamesEachWindow()
PASS   : ModelCatalogTests::theWorkersLimitsObjectIsReadWindowsAndStatus()
PASS   : ModelCatalogTests::exhaustedIsASpentWindowUntilItsReset()
PASS   : ModelCatalogTests::thePrioritySkipsAnExhaustedPresetUntilItResets()
PASS   : ModelCatalogTests::resetTextIsTheLimitsLinesWording()
PASS   : ModelCatalogTests::filterMatchesEveryWordAnywhere()
PASS   : ModelCatalogTests::cleanupTestCase()
Totals: 22 passed, 0 failed, 0 skipped, 0 blacklisted, 609ms

$ QT_QPA_PLATFORM=offscreen ./relay-modelpicker-tests
PASS   : ModelPickerTests::initTestCase()
PASS   : ModelPickerTests::rowsFollowRankAndTheCurrentOneIsSelected()
PASS   : ModelPickerTests::theReasoningRowFollowsTheHighlightedModel()
PASS   : ModelPickerTests::aPickReturnsTheKeyAndTheLevelAndRemembersIt()
PASS   : ModelPickerTests::typingFiltersToOneFlatList()
PASS   : ModelPickerTests::theSortMenuReordersAndPersists()
PASS   : ModelPickerTests::anExhaustedSubscriptionIsGreyedStillSelectableAndSaysWhenItResets()
PASS   : ModelPickerTests::customizeClosesAndOpensThePage()
PASS   : ModelPickerTests::cleanupTestCase()
Totals: 9 passed, 0 failed, 0 skipped, 0 blacklisted, 194ms

$ PYTHONPATH=backend python3 -m unittest tests.test_provider.HTTPTests
Ran 5 tests in 0.585s

OK
```

What marks a provider exhausted (relay::models::exhaustedUntil): a limit window at used_percent >= 100, or the provider's own status "rejected" (usage_limits.status), while resets_at is still ahead or unknown. Sources: the worker's usage_limits event (guests), hosted_quota / a quota_exhausted refusal (Relay Free, one daily window), and the pane's own 30-minute cool-off when a provider with no quota endpoint answered 429 through the transport's retries and the turn then failed over or died on it.
