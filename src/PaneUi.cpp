// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Pane.h"

void Pane::buildUi() {
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(8, 6, 8, 8); layout->setSpacing(6);
        // Pane header (issue JRWQ): what this pane is doing, written by the model and refreshed as
        // the work moves on; the directory keeps its place on the right, smaller and dim. Double
        // click the title to name the pane by hand (/rename does the same without the mouse).
        auto *header = new QWidget;
        header->setObjectName(QStringLiteral("paneHeader"));
        // The whole row is the pane's drag handle (headerDragEvent), so it says so with the cursor.
        header->setCursor(Qt::OpenHandCursor);
        auto *headerRow = new QHBoxLayout(header);
        m_headerLayout = headerRow;
        headerRow->setContentsMargins(0, 0, 0, 0);
        headerRow->setSpacing(8);
        m_titleLabel = new QLabel; m_titleLabel->setTextFormat(Qt::PlainText);
        m_titleLabel->setObjectName(QStringLiteral("paneTitle"));
        // No caret here: the title is dragged far more often than it is renamed, so it inherits
        // the header's open hand. Double click still renames it, as the tooltip says.
        // The text is elided in updateHeader(), so the label asks for exactly what it shows.
        m_titleLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        // ...and a QLabel's own minimum is that whole text, which made the pane's minimum width
        // follow the title the model keeps rewriting as the work moves on: a splitter must satisfy
        // every child's minimum, so a longer title widened the pane and took the pixels off its
        // neighbours (card #SDXE). An explicit minimum REPLACES the computed one, so this is the
        // ladder's own title floor and nothing to do with the text — the label still asks for what
        // it shows through its size hint, and still gets it whenever the header has the room.
        m_titleLabel->setMinimumWidth(relay::panes::kTitleFloorPx);
        m_titleLabel->installEventFilter(this);
        m_titleEdit = new QLineEdit;
        m_titleEdit->setObjectName(QStringLiteral("paneTitleEdit"));
        m_titleEdit->setVisible(false);
        m_titleEdit->setMaxLength(relay::titles::kMaxUserTitle);
        m_titleEdit->setPlaceholderText(QStringLiteral("Name this pane — empty goes back to the model's name"));
        m_titleEdit->setMinimumWidth(220);
        m_titleEdit->installEventFilter(this);
        connect(m_titleEdit, &QLineEdit::returnPressed, this, [this] { commitRename(); });
        m_titleAuto = new QLabel(QStringLiteral("auto"));
        m_titleAuto->setObjectName(QStringLiteral("paneAuto"));
        m_titleAuto->setVisible(false);
        // The claims chip (#C7PF, #0FBB): the Board cards this pane is working, immediately before
        // the title — `#K7Q2`, or `#K7Q2 (3)` with the latest first. A click, Space or Enter opens
        // the one card, or the list when there are several (openClaimsMenu). It is a button of
        // its own, so Tab reaches it and it is no part of the header's drag handle;
        // refreshCardChip() decides whether it is up and what it says.
        class ClaimsChip : public QToolButton {
        protected:
            void keyPressEvent(QKeyEvent *event) override {
                if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { click(); return; }
                QToolButton::keyPressEvent(event);
            }
        };
        m_cardChip = new ClaimsChip;
        m_cardChip->setObjectName(QStringLiteral("paneCardChip"));
        m_cardChip->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_cardChip->setFocusPolicy(Qt::TabFocus);
        m_cardChip->setCursor(Qt::PointingHandCursor);
        m_cardChip->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        m_cardChip->hide();
        connect(m_cardChip, &QToolButton::clicked, this, [this] {
            const QStringList ids = cardChipCards();
            if (ids.size() == 1) { if (onOpenCard) onOpenCard(ids.first()); return; }
            openClaimsMenu();
        });
        m_cwdLabel = new QLabel; m_cwdLabel->setTextFormat(Qt::PlainText);
        m_cwdLabel->setObjectName(QStringLiteral("paneCwd"));
        // Clicking the directory line opens it in the explorer pane.
        m_cwdLabel->setCursor(Qt::PointingHandCursor);
        m_cwdLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        // A QLabel's minimum is its whole text. With the long "TERMINAL <path> │ AGENT
        // WORKSPACE <path>" form this label carried until #0STR that made the pane refuse to go
        // under ~1000 px, so a pane opened beside it (the Switchboard, 2026-09-17) got a third of
        // the window instead of half. The minimum goes, and the text is elided from the left in
        // updateHeader() to
        // exactly the room the header has left, so the label asks for no more than it shows.
        // Letting the layout do the squeezing instead cut the path mid-glyph — a crowded header
        // ended in a stray half of a character where the directory should be. The tooltip has
        // both paths in full whatever is shown.
        m_cwdLabel->setMinimumWidth(1);
        m_cwdLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_cwdLabel->installEventFilter(this);
        // Not selectable any more: dragging across the path is how you move the pane now, and a
        // half-selected path is a poor trade for that. The tooltip still has both paths in full.
        m_consoleKindChip = new QLabel(consoleKind());
        m_consoleKindChip->setObjectName(QStringLiteral("paneConsoleKind"));
        m_consoleKindChip->setTextFormat(Qt::PlainText);
        m_consoleKindChip->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        m_consoleKindChip->setVisible(hasShell());
        headerRow->addWidget(m_consoleKindChip, 0);
        headerRow->addWidget(m_cardChip, 0);
        headerRow->addWidget(m_titleLabel, 0);
        headerRow->addWidget(m_titleEdit, 1);
        headerRow->addWidget(m_titleAuto, 0);
        headerRow->addStretch(1);
        headerRow->addWidget(m_cwdLabel, 0);
        m_headerWidget = header;
        layout->addWidget(header);
        m_terminalHost = new QWidget;
        auto *terminalLayout = new QVBoxLayout(m_terminalHost); terminalLayout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_terminalHost, 1);
        m_terminalHost->installEventFilter(this);   // the toast follows the terminal host's corner
        auto *composer = new QFrame; composer->setFrameShape(QFrame::StyledPanel);
        composer->setObjectName(QStringLiteral("composer"));
        // The prompt box never sets the pane's minimum width. Its chip row is wider than a pane in
        // a three-pane split, and a splitter that cannot satisfy every minimum redistributes as
        // soon as one of them changes — which is what made taking control (Ctrl+H) shrink a pane
        // to almost nothing (#G152). Ignored means the row is squeezed instead, as it already is
        // in a narrow pane; the pane's minimum width stays the terminal's.
        composer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_composer = composer;
        auto *composerLayout = new QVBoxLayout(composer);
        auto *routeRow = new QHBoxLayout;
        // The strip under the prompt box (owner design, 2026-09-17): directory, Switchboard and
        // tasks on the left, context left / model / microphone on the right, each in a Warp-style
        // chip. Where the line goes is not in the strip: the mode chip sits in the prompt box's
        // top-right corner (owner, 2026-09-17), on the text it routes, with the `!` / `*` and
        // password chips that qualify it. The corner is a column of its own, so text wraps before
        // it rather than running underneath.
        auto *corner = new QHBoxLayout;
        corner->setContentsMargins(0, 0, 0, 0);
        corner->setSpacing(6);
        m_cwdChip = new QToolButton;
        m_cwdChip->setObjectName(QStringLiteral("stripChip"));
        m_cwdChip->setFocusPolicy(Qt::NoFocus);
        m_cwdChip->setCursor(Qt::PointingHandCursor);
        connect(m_cwdChip, &QToolButton::clicked, this, [this] {
            if (!onOpenPath) return;
            if (m_login.active) {
                if (loginReachable() && !m_login.cwd.isEmpty()) onOpenPath(relay::remote::folderUrl(loginHost(), m_login.cwd), 0);
                else status(QStringLiteral("Remote folder is not available until the SSH shell is ready."));
            } else onOpenPath(m_cwd, 0);
        });
        routeRow->addWidget(m_cwdChip);
        m_shareChip = new QToolButton;
        m_shareChip->setObjectName(QStringLiteral("stripChip"));
        m_shareChip->setFocusPolicy(Qt::NoFocus);
        m_shareChip->setCursor(Qt::PointingHandCursor);
        m_shareChip->setIcon(stripIcon(QStringLiteral("share")));
        if (m_shareChip->icon().isNull()) m_shareChip->setText(QStringLiteral("↗"));
        m_shareChip->setIconSize(QSize(13, 13));
        m_shareChip->setAccessibleName(QStringLiteral("Share this pane or open Sharing"));
        connect(m_shareChip, &QToolButton::clicked, this, [this] { shareChipPressed(); });
        routeRow->addWidget(m_shareChip);
        updateShareChip();
        m_modeChip = new QToolButton;
        m_modeChip->setObjectName(QStringLiteral("stripChip"));
        m_modeChip->setFocusPolicy(Qt::NoFocus);
        m_modeChip->setCursor(Qt::PointingHandCursor);
        m_modeChip->setPopupMode(QToolButton::InstantPopup);
        {
            auto *menu = new QMenu(m_modeChip);
            for (const auto &pair : {std::pair<const char *, const char *>{"auto", "auto"},
                                     {"shell", "terminal"}, {"agent", "agent"}}) {
                const QString value = QString::fromLatin1(pair.first);
                menu->addAction(QString::fromLatin1(pair.second), this, [this, value] { setMode(value); focusInput(); });
            }
            auto *programMode = menu->addAction(QStringLiteral("program"), this, [this] {
                setMode(QStringLiteral("program")); focusInput();
            });
            connect(menu, &QMenu::aboutToShow, this, [this, programMode] {
                programMode->setEnabled(!m_native && !m_altScreen && processBusy());
            });
            m_modeChip->setMenu(menu);
        }
        setupTaskUpdates();
        // `!` / `*` typed first in an empty prompt: terminal / agent mode for this submission.
        m_prefixChip = new QLabel;
        m_prefixChip->setObjectName(QStringLiteral("prefixChip"));
        m_prefixChip->hide();
        corner->addWidget(m_prefixChip);
        // "password for sudo" while the prompt box is masked.
        m_secretChip = new QLabel;
        m_secretChip->setObjectName(QStringLiteral("secretChip"));
        m_secretChip->setTextFormat(Qt::PlainText);
        m_secretChip->setToolTip(QStringLiteral("The line is written to the program and never stored"));
        m_secretChip->hide();
        corner->addWidget(m_secretChip);
        corner->addWidget(m_modeChip);
        // The routing verdict has no chip of its own: it is the mode chip's tooltip. The label
        // survives only as the place that text and tooltip live, so it is parented to the composer
        // and never added to a layout or shown. A parentless QWidget that is shown becomes a
        // top-level window of its own: that is the tiny second window of #RDQ7.
        m_routeLabel = new QLabel(composer);
        m_routeLabel->hide();
        m_opaqueHint = new QLabel;
        m_opaqueHint->setObjectName(QStringLiteral("opaqueHint"));
        m_opaqueHint->hide();
        routeRow->addWidget(m_opaqueHint, 1);
        routeRow->addStretch(1);
        buildSessionControls(routeRow);        // plan chip left; context chip by the model
        m_modelBox = new CurrentTextComboBox;
        m_modelBox->setObjectName(QStringLiteral("statusPicker"));
        m_modelBox->setAccessibleName(QStringLiteral("Agent model"));
        m_modelBox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        m_modelBox->setFocusPolicy(Qt::TabFocus);
        connect(m_modelBox, qOverload<int>(&QComboBox::activated), this, [this](int index) {
            modelBoxPicked(m_modelBox->itemData(index).toString());
        });
        routeRow->addWidget(m_modelBox);
        if (m_effortBox) routeRow->addWidget(m_effortBox);   // the level, right of the model (owner, 2026-09-20)
        // A chord pressed while one of the two boxes is open (owner, 2026-09-20: "if the model box
        // is open and I press Alt+E, get to the effort without changing anything, and vice versa;
        // in general an open box doesn't interrupt other hotkeys"): the popup closes with nothing
        // picked and the key goes on to the window, which runs it as it would have.
        passHotkeysThrough(m_modelBox, QStringLiteral("agent.modelBox"));
        if (m_effortBox) passHotkeysThrough(m_effortBox, QStringLiteral("agent.effortBox"));
        // Voice transcription: the chip toggles recording, the hold key is push-to-talk.
        m_mic = new QToolButton;
        m_mic->setObjectName(QStringLiteral("stripChip"));
        m_mic->setFocusPolicy(Qt::NoFocus);
        m_mic->setIcon(stripIcon(QStringLiteral("mic")));
        m_mic->setIconSize(QSize(14, 14));
        m_mic->setAccessibleName(QStringLiteral("Voice transcription"));
        connect(m_mic, &QToolButton::clicked, this, [this] { toggleVoice(false); });
        routeRow->addWidget(m_mic);
        updateVoiceChip();
        auto *cancel = new QToolButton;
        cancel->setObjectName(QStringLiteral("interruptButton"));
        const QString cancelIcon = relay::theme::themeDataDir() + QStringLiteral("/icons/cancel.svg");
        if (QFileInfo::exists(cancelIcon)) cancel->setIcon(QIcon(cancelIcon)); else cancel->setText(QStringLiteral("⊘"));
        cancel->setToolTip(QStringLiteral("Interrupt the running command (Esc in the prompt box)"));
        cancel->setAccessibleName(QStringLiteral("Interrupt shell"));
        cancel->setFocusPolicy(Qt::NoFocus);
        connect(cancel, &QToolButton::clicked, this, [this] { interruptShell(); });
        cancel->hide();
        m_interruptButton = cancel;
        routeRow->addWidget(cancel);
        refreshPickers();
        routeRow->setContentsMargins(2, 0, 2, 0);
        routeRow->setSpacing(6);
        m_editor = new RichEditor;
        // **Do not give this editor an objectName.** `RichEditor`'s constructor already sets one
        // -- `composerEditor` -- and three things read it rather than the type: the stylesheet
        // rule that makes it borderless in the prompt font, `theme::polishWindow`, which names
        // the frame around it `composer` and this widget `pane` by finding it, and
        // `RelayWindow::repolishLeaf`, which follows `relayActive` to that frame. Renaming it to
        // something a QA driver would rather type cost all three: the drive of card #AGNT caught
        // the editor painting its own default frame inside the composer's. `composerEditor` **is**
        // the stable name, in a terminal pane and in every console, and a driver asks for it under
        // whichever host it is looking at.
        // Up and Down walk this pane's own history, kept in a file under the pane's layout id so
        // it survives a restart and a close-and-reopen, and stays this pane's alone (owner report,
        // 2026-09-19: "the up/down history seems to be getting commands from other panes, not just
        // mine"). src/PromptHistory.h has the rules; initRestore() re-points this at the saved id
        // when the pane is being restored.
        m_editor->useHistoryFile(promptHistoryPath());
        m_highlighter = new relay::InputHighlighter(m_editor->document());
        QTimer::singleShot(0, this, [this] { refreshDestinationColor(); });
        m_editor->setAutoHeight(1, 1000);   // the pane height caps long drafts
        m_editor->setHeightLimit(std::max(m_editor->minimumHeight(), height() * 2 / 3 - 72));
        auto *inputRow = new QHBoxLayout;
        inputRow->setContentsMargins(0, 0, 0, 0);
        inputRow->setSpacing(6);
        auto *inputColumn = new QVBoxLayout;
        inputColumn->setContentsMargins(0, 0, 0, 0);
        inputColumn->addWidget(m_editor);
        inputRow->addLayout(inputColumn, 1);
        auto *cornerColumn = new QVBoxLayout;
        cornerColumn->setContentsMargins(0, 0, 0, 0);
        cornerColumn->addLayout(corner);
        cornerColumn->addStretch(1);
        inputRow->addLayout(cornerColumn);
        // The "Relaying · …" line (cards #4E13, #HQ2B, #RR0G, #R3YN): agent work in the agent's
        // violet, saying what
        // it is doing right now ("Relaying · reading src/Pane.h… · 12 s · Esc stops"), a terminal
        // program in the terminal's blue ("Relaying · sleep…"), left-aligned with the prompt text
        // and in the normal weight above the prompt. The turn clock lived in the strip under the box until
        // #4E13; it moved up here and was restyled into this line — one place, one verb, the
        // colour saying whose work it is.
        // The action row (#PBX1): things the agent can do here that need no typing, left-aligned
        // above the busy line, inside the composer frame's column so it moves with the box. Built
        // from the context — empty and hidden for a terminal pane — and created here so that
        // rebuildActionRow() has somewhere to put buttons the moment a context is set.
        m_actionRow = new QWidget(composer);
        m_actionRow->setObjectName(QStringLiteral("agentActionRow"));
        m_actionRowLayout = new QHBoxLayout(m_actionRow);
        m_actionRowLayout->setContentsMargins(0, 0, 0, 0);
        m_actionRowLayout->setSpacing(6);
        m_actionRow->hide();
        composerLayout->addWidget(m_actionRow);
        // Terminal output rides along without anything in the prompt box (owner, 2026-09-22 #TCXT):
        // previewing, attaching, removing it and the pane's sharing are `/terminal`'s menu.
        relay::SettingsWatch::instance().listen(this, [this] { syncTerminalContext(); });
        m_busyLine = new PaneBusyLine(this);
        m_busyLine->setPromptEditor(m_editor);   // the row's left edge is the prompt text's (#HQ2B)
        // The relay mark at its left opens this pane's Activity pane (#4X53) — the same call the
        // keymap action and the palette make, so a second click brings the open one forward.
        // `fromMouse` is what teaches the key the first time (the `internals.open` hint).
        m_busyLine->onOpenActivity = [this] { openInternalsPane(QString(), true); };
        composerLayout->addLayout(inputRow);
        // Password prompts (checkPasswordPrompt): the prompt box becomes a masked field whose
        // line goes to the running program. It is a separate widget so the password can never
        // reach the composer's document, its history, its undo stack or route assist.
        m_secretEdit = new QLineEdit;
        m_secretEdit->setObjectName(QStringLiteral("secretEditor"));
        m_secretEdit->setEchoMode(QLineEdit::Password);
        m_secretEdit->setAccessibleName(QStringLiteral("Password for the running program"));
        m_secretEdit->setPlaceholderText(QStringLiteral("Password · Enter sends it to the program, Esc cancels"));
        m_secretEdit->hide();
        connect(m_secretEdit, &QLineEdit::returnPressed, this, [this] { submitSecret(); });
        inputColumn->addWidget(m_secretEdit);
        composerLayout->addLayout(routeRow);
        // No key-hints row: Ctrl+? lists every shortcut, and the strip stays quiet.
        m_help = nullptr;
        buildTranscript();
        layout->addWidget(m_transcript);
        // The queue strip is a row of the pane's own column, directly under the terminal, and not
        // an overlay floating over it any more: it takes real layout space so the terminal host
        // shrinks and the shell reflows into what is left (owner report, 2026-09-18: "the terminal
        // needs to move up, rather than being covered up"). The width policy is the prompt box's,
        // and for the same reason: a queued line is wider than a pane in a three-pane split, and a
        // minimum that wide would move this pane's minimum and make the splitter redistribute
        // every pane in the row (#G152). placeQueueStrip() sets the height it asks for.
        m_queueStrip = new QFrame(this);
        m_queueStrip->setObjectName(QStringLiteral("queueStrip"));
        m_queueStrip->setAttribute(Qt::WA_StyledBackground);
        m_queueStrip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *queueLayout = new QVBoxLayout(m_queueStrip); queueLayout->setContentsMargins(10, 6, 6, 6); queueLayout->setSpacing(2);
        m_queueStrip->hide();
        layout->insertWidget(layout->indexOf(m_terminalHost) + 1, m_queueStrip);
        // A full-screen program (vim, htop) or an ssh session owns the screen. Relay no longer
        // switches to native input by itself; this button, or Ctrl+H, hands the keyboard over.
        // Both buttons live in one floating banner over the top of the terminal, next to the
        // line that says what the program is asking ("apt is asking: Do you want to continue?
        // [Y/n]"), so the question and the two ways to answer it are in one place.
        m_programBar = new QFrame(this);
        m_programBar->setObjectName(QStringLiteral("programBanner"));
        m_programBar->setAttribute(Qt::WA_StyledBackground);
        auto *bannerRow = new QHBoxLayout(m_programBar);
        bannerRow->setContentsMargins(10, 4, 6, 4);
        bannerRow->setSpacing(8);
        m_programLabel = new QLabel(m_programBar);
        m_programLabel->setObjectName(QStringLiteral("programBannerLabel"));
        m_programLabel->setTextFormat(Qt::PlainText);
        bannerRow->addWidget(m_programLabel, 1);
        // "Let the agent drive" hands the program over; while it drives, the same button is
        // "Take over" and gives the keyboard back (card C1HH).
        m_delegateButton = new QPushButton(m_programBar);
        m_delegateButton->setObjectName(QStringLiteral("delegateChip"));
        m_delegateButton->setCursor(Qt::PointingHandCursor);
        m_delegateButton->setFocusPolicy(Qt::NoFocus);
        connect(m_delegateButton, &QPushButton::clicked, this, [this] {
            const bool wasDriving = m_delegated;
            delegateProgram();
            const QString action = wasDriving ? QStringLiteral("control.human") : QStringLiteral("program.delegate");
            hint(QStringLiteral("program.delegate.mouse"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(action),
                                                wasDriving ? QStringLiteral("take the program back")
                                                           : QStringLiteral("hand the program to the agent")));
        });
        bannerRow->addWidget(m_delegateButton);
        m_takeControl = new QPushButton(m_programBar);
        m_takeControl->setObjectName(QStringLiteral("takeControlChip"));
        m_takeControl->setCursor(Qt::PointingHandCursor);
        m_takeControl->setFocusPolicy(Qt::NoFocus);
        connect(m_takeControl, &QPushButton::clicked, this, [this] {
            takeControl();
            hint(QStringLiteral("control.human.mouse"),
                 relay::ShortcutHints::nextTime(Keymap::instance().shortcutText(QStringLiteral("control.human")), QStringLiteral("take control")));
        });
        bannerRow->addWidget(m_takeControl);
        m_programBar->hide();
        // A guest's permission question (GT7X, 26.4): the PermissionRequest hook holds the shim open,
        // and this bar is where the user answers it. The same banner idiom, the opposite
        // corner, so a question and the program banner never sit on top of each other.
        m_guestBar = new QFrame(this);
        m_guestBar->setObjectName(QStringLiteral("programBanner"));
        m_guestBar->setAttribute(Qt::WA_StyledBackground);
        auto *questionRow = new QHBoxLayout(m_guestBar);
        questionRow->setContentsMargins(10, 4, 6, 4);
        questionRow->setSpacing(8);
        m_guestBarLabel = new QLabel(m_guestBar);
        m_guestBarLabel->setObjectName(QStringLiteral("programBannerLabel"));
        m_guestBarLabel->setTextFormat(Qt::PlainText);
        questionRow->addWidget(m_guestBarLabel, 1);
        // Unlike the program banner's chips these take the keyboard: the guest is blocked on the
        // answer, and a question that can only be answered with the mouse is one a touch-typist
        // cannot answer at all. showGuestQuestion() focuses Allow; guestBarKey() adds Y/N/Enter/Esc
        // and hands any other key straight back to the pane's own input.
        m_guestAllow = new QPushButton(m_guestBar);
        m_guestAllow->setObjectName(QStringLiteral("delegateChip"));
        m_guestAllow->setCursor(Qt::PointingHandCursor);
        m_guestAllow->setFocusPolicy(Qt::StrongFocus);
        m_guestAllow->setText(QStringLiteral("Allow"));
        m_guestAllow->setToolTip(QStringLiteral("Allow this tool call · Y or Enter"));
        m_guestAllow->installEventFilter(this);
        connect(m_guestAllow, &QPushButton::clicked, this, [this] { answerGuestPermission(true); });
        questionRow->addWidget(m_guestAllow);
        m_guestDeny = new QPushButton(m_guestBar);
        m_guestDeny->setObjectName(QStringLiteral("takeControlChip"));
        m_guestDeny->setCursor(Qt::PointingHandCursor);
        m_guestDeny->setFocusPolicy(Qt::StrongFocus);
        m_guestDeny->setText(QStringLiteral("Deny"));
        m_guestDeny->setToolTip(QStringLiteral("Deny this tool call · N or Esc"));
        m_guestDeny->installEventFilter(this);
        connect(m_guestDeny, &QPushButton::clicked, this, [this] { answerGuestPermission(false); });
        questionRow->addWidget(m_guestDeny);
        m_guestBar->setFocusPolicy(Qt::StrongFocus);
        m_guestBar->installEventFilter(this);
        m_guestBar->hide();
        // Status is chrome around the input, not input (#R3YN): it sits immediately above the
        // rounded composer frame, shared by terminal panes and shell-less helper consoles alike.
        // Card #H2KQ: Take over / Take control sits beside the Relaying line (the top-right
        // program bubble is retired). The row collapses to nothing when both are hidden.
        auto *busyRow = new QWidget;
        busyRow->setObjectName(QStringLiteral("busyRow"));
        auto *busyRowLayout = new QHBoxLayout(busyRow);
        busyRowLayout->setContentsMargins(0, 0, 0, 0);
        busyRowLayout->setSpacing(8);
        busyRowLayout->addWidget(m_busyLine, 1);
        m_busyAction = new QToolButton;
        m_busyAction->setObjectName(QStringLiteral("busyAction"));
        m_busyAction->setCursor(Qt::PointingHandCursor);
        m_busyAction->setFocusPolicy(Qt::NoFocus);
        m_busyAction->setVisible(false);
        connect(m_busyAction, &QToolButton::clicked, this, [this] {
            if (m_busyActionTakeOver) takeOverFromAgent();
            else takeControl();
        });
        busyRowLayout->addWidget(m_busyAction, 0, Qt::AlignVCenter);
        m_programInputAction = new QToolButton;
        m_programInputAction->setObjectName(QStringLiteral("programInputAction"));
        m_programInputAction->setCursor(Qt::PointingHandCursor);
        m_programInputAction->setFocusPolicy(Qt::NoFocus);
        m_programInputAction->hide();
        connect(m_programInputAction, &QToolButton::clicked, this, [this] {
            setMode(QStringLiteral("program"));
            focusInput();
        });
        busyRowLayout->addWidget(m_programInputAction, 0, Qt::AlignVCenter);
        layout->addWidget(busyRow);
        layout->addWidget(composer);
        setupSubagentsUi(layout);   // subagents UI: running-agents list beneath the composer
        setupJobsUi(layout);        // commands the agent left running, beneath that
        updatePaths();
    }
