"""The action catalog reaches every registered action (#ACDG), and the window's key wiring for
Sessions & Projects (#SPSG), the Actions palette (#MAGP) and key precedence (#KYPR) is in place.

`RelayWindow` is one header of the single `relay` translation unit and cannot be built on its own in
a test, so these read the source: the Keymap's registered ids, `RelayWindow::runAction`,
`rootItems()` with its `appendRegisteredActions()` pass and the `catalogEquivalents()` exclusion
list. The palette itself is driven headless in tests/actionpalette_test.cpp; the widgets that
declare local keys are driven in tests/settingspane_test.cpp and tests/filepanes_test.cpp.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEYMAP = (ROOT / 'src' / 'Keymap.h').read_text(encoding='utf-8')
WINDOW = (ROOT / 'src' / 'RelayWindow.h').read_text(encoding='utf-8')


def registered_ids():
    return re.findall(r'\badd\("([A-Za-z.]+)",', KEYMAP)


def member(name):
    """The body of a RelayWindow member function, from its signature to the next member at the same
    indentation."""
    match = re.search(r'\n    [^\n]*\b' + re.escape(name) + r'\([^\n]*\{\n', WINDOW)
    if not match:
        raise AssertionError(f'{name} not found in src/RelayWindow.h')
    end = re.search(r'\n    \}\n', WINDOW[match.end():])
    return WINDOW[match.start():match.end() + end.end()]


def literals(text):
    return set(re.findall(r'QStringLiteral\("([^"]+)"\)', text))


class ActionCatalogTest(unittest.TestCase):
    def test_every_registered_action_is_in_the_catalog(self):
        # The hand-made rows, then one plain row for every registered id they miss.
        root = member('rootItems')
        append = root.index('appendRegisteredActions(items);')
        self.assertLess(append, root.index('const QList<QPair<QString, QStringList>> groups{'),
                        'the registry pass must run before the rows are put into groups')
        body = member('appendRegisteredActions')
        self.assertIn('Keymap::instance().actions()', body)
        self.assertIn('catalogEquivalents().contains(action.id)', body)
        self.assertIn('item.children', body, 'submenu children count as present')

    def test_the_exclusion_list_is_only_real_equivalents(self):
        ids = set(registered_ids())
        excluded = literals(member('catalogEquivalents'))
        self.assertEqual(excluded, {'agent.resume', 'projects.open', 'globals.open', 'control.prompt',
                                    'palette.open', 'help.shortcuts'})
        self.assertLessEqual(excluded, ids, 'an excluded id that is not registered is a stale entry')
        # Each has the row that stands for it.
        sessions = literals(member('sessionsMenuItems'))
        self.assertLessEqual({'conversations.open', 'projects.open', 'globals.open'}, sessions)
        self.assertNotIn('sessions.background', sessions)
        self.assertIn('QStringLiteral("background.open")',
                      (ROOT / 'src' / 'RelayWindow.cpp').read_text(encoding='utf-8'))
        root = member('rootItems')
        self.assertIn('QStringLiteral("sessions.open"), agent, QStringLiteral("Sessions & Projects")', root)
        self.assertIn('QStringLiteral("control.human"), pane && pane->isNative()', root)
        self.assertNotIn('QStringLiteral("control.prompt")', root.split('appendRegisteredActions')[0])
        run = member('runActionNow')
        for alias, tab in (('projects.open', 'projects'), ('globals.open', 'globals')):
            self.assertIn(f'id == QStringLiteral("{alias}")) toggleSessionsPane(pane, QStringLiteral("{tab}"))', run)
        self.assertIn('id == QStringLiteral("agent.resume") || id == QStringLiteral("conversations.open"))\n'
                      '            toggleSessionsPane(pane, QStringLiteral("sessions"))', run)

    def test_one_sessions_row_not_three(self):
        root = member('rootItems').split('appendRegisteredActions')[0]
        for label in ('QStringLiteral("Projects")', 'QStringLiteral("Globals")', 'QStringLiteral("Sessions…")'):
            self.assertNotIn(label, root)

    def test_run_action_answers_every_registered_id(self):
        # A catalog row for an id runAction ignores would be a row that does nothing.
        run = member('runActionNow')
        missing = [i for i in registered_ids()
                   if f'"{i}"' not in run and not i.startswith('terminal.zoom')]
        self.assertEqual(missing, [])

    def test_palette_keys_open_the_palette(self):
        run = member('runActionNow')
        self.assertIn('id == QStringLiteral("palette.open")) togglePalette();', run)
        self.assertIn('togglePalette();', member('openShortcutsTab'))
        self.assertNotIn('toggleSettingsPane(true)', run)
        self.assertIn('toggleSettingsPane(true)', member('rootItems'), 'the Shortcut list row keeps the Actions pane')
        toggle = member('togglePalette')
        for part in ('searchableActions()', 'paletteForThisPane()', 'palette/recent', 'rememberPaletteChoice(key)',
                     'setToggleKeys', 'setEditShortcut'):
            self.assertIn(part, toggle)
        # Right-click › Change shortcut… takes the new keys and writes them (Keymap::setBinding).
        edit = member('editShortcutOf')
        for part in ('QKeySequenceEdit', 'actionForKey(chosen)', 'setBinding(key, {chosen})', 'setBinding(key, {})'):
            self.assertIn(part, edit)
        here = member('paletteForThisPane')
        for action in ('pane.restartShell', 'agent.stop', 'agent.stopAllSubagents', 'control.human', 'prompt.clear'):
            self.assertIn(f'QStringLiteral("{action}")', here)

    def test_control_human_is_one_toggle(self):
        run = member('runActionNow')
        self.assertIn('if (pane->isNative()) pane->showPrompt(); else pane->takeControl();', run)
        self.assertIn('id == QStringLiteral("prompt.clear")) pane->clearPrompt();', run)

    def test_local_keys_and_composer_only_keys(self):
        dispatch = member('eventFilter')
        local = dispatch.index('property("relayLocalKeys")')
        self.assertLess(local, dispatch.index('const QString id = Keymap::instance().match(key);'),
                        'a widget\'s own keys are checked before the keymap is')
        self.assertIn('m_palette->isAncestorOf(widget)', dispatch)
        # Plain Ctrl+H / Ctrl+Q only from the prompt box; the Shift forms from anywhere.
        self.assertIn('(id == QStringLiteral("control.human") || id == QStringLiteral("prompt.clear")) && !shifted', dispatch)
        settings = (ROOT / 'src' / 'SettingsPane.cpp').read_text(encoding='utf-8')
        self.assertIn('m_search->setProperty("relayLocalKeys", QStringList{QStringLiteral("Ctrl+N"), QStringLiteral("Ctrl+P")})',
                      settings)
        files = (ROOT / 'src' / 'FilePanes.cpp').read_text(encoding='utf-8')
        self.assertIn('m_view->setProperty("relayLocalKeys", QStringList{QStringLiteral("Alt+Up"), QStringLiteral("Left"), QStringLiteral("Right")})', files)
        self.assertIn('m_filter->setProperty("relayLocalKeys", QStringList{QStringLiteral("Alt+Up")})', files)

    def test_board_slash_label_is_board(self):
        settings = (ROOT / 'src' / 'SettingsPane.cpp').read_text(encoding='utf-8')
        self.assertIn('{QStringLiteral("board.open"), QStringLiteral("/board")}', settings)

    def test_no_hint_prints_a_key_that_moved_away(self):
        # These ids lost their default keys (#QWAS, #SPSG); a hint printing them prints nothing.
        unbound = ('projects.open', 'globals.open', 'agent.resume', 'conversations.open', 'control.prompt')
        for path in ('src/RelayWindow.h', 'src/Pane.h', 'src/PaneStatus.cpp'):
            text = (ROOT / path).read_text(encoding='utf-8')
            for action in unbound:
                self.assertNotIn(f'shortcutText(QStringLiteral("{action}"))', text, f'{path} prints {action}')
        status = (ROOT / 'src' / 'PaneStatus.cpp').read_text(encoding='utf-8')
        self.assertIn('{QStringLiteral("sessions"), QStringLiteral("sessions.open"),', status)


if __name__ == '__main__':
    unittest.main()
