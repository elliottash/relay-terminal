// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Relay-to-Relay: a pane shared by another desktop, opened in this Relay as the owner's own device.
//
// The other desktop is the model. It sends the screen as rows of styled runs (`screen_snapshot`,
// `screen_diff`, `history`) and everything under the terminal as one `pane_state`
// (docs/REMOTE-PROTOCOL.md sections 6.5 and 16). This file draws both and formats nothing: every
// label, hint and model name is the desktop's, every queue action offered is one the row lists,
// and every string from the wire is drawn as plain text.
//
// The encrypted session is a Python sidecar, `remote/viewer.py`, in the same arrangement as the
// sharing side's `remote/gui_host.py` (src/RemoteShare.h): line JSON on stdio, no crypto here. The
// contract, Qt → viewer: pair, connect, forget, open, close, send, stop; viewer → Qt: status, code,
// paired, welcome, message (every server message verbatim), error.
//
// Joining somebody else's share with a meeting code and PIN is a second sidecar, `python3 -m
// remote.viewer --guest`, so being a guest there never disturbs this Relay's own device session.
// Its contract, Qt → viewer: join {code, pin, name, platform, rendezvous}, connect (rejoin the stored
// guest record), leave, open, close, send, stop; viewer → Qt: status (unjoined, joining, knocking,
// connecting, connected, reconnecting, offline), code (the knock's check code), joined {desktop,
// role, panes, expires}, welcome, message, error {message, reason}, ended {message}. A guest is
// never sent `pane_state` (docs/REMOTE-PROTOCOL.md section 10.1), so a guest pane draws the screen,
// the keyboard's holder and, for an editor, a prompt box whose prompts wait for the owner.
//
// Three layers, so the rules are tested without a window or a sidecar (tests/remotepane_test.cpp):
//   remoteview::ScreenModel  the cell grid and the scrollback column, app/screen.js's rules in C++
//   RemotePane               the pane widget, talking through a Sink it is handed
//   RemoteViewer             a sidecar process: instance() for this Relay's own device, guest() for
//                            the share it joined as a guest
#include "PaneView.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QDialog>
#include <QFont>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace relay {

class RemoteViewer;

namespace remoteview {

// relay::CellAttr, as app/screen.js names the bits.
enum Attr : int {
    Bold = 1 << 0, Italic = 1 << 1, Underline = 1 << 2, DoubleUnderline = 1 << 3,
    CurlyUnderline = 1 << 4, Blink = 1 << 5, Reverse = 1 << 6, Conceal = 1 << 7,
    Strike = 1 << 8, Faint = 1 << 9,
};

// One run of identical style: `[text, fg, bg, attrs]` on the wire.
struct Seg {
    QString text;
    quint32 fg = 0, bg = 0;
    int attrs = 0;
};
using Segs = QVector<Seg>;
Segs segsOf(const QJsonArray &runs);

// One grid cell after the runs are laid out: a wide glyph occupies two cells, the second `tail`.
struct Cell {
    QString text;          // one grapheme, or " " for an empty cell
    quint32 fg = 0, bg = 0;
    int attrs = 0;
    bool tail = false;
    bool cursor = false;
};
// How many columns a codepoint takes: 0 for a combining mark, 2 for an East Asian wide glyph.
int columnsOf(char32_t codepoint);
// The runs as `cols` cells, padded with blanks. `cursorCol` < 0 puts no cursor on the row.
QVector<Cell> cellsOf(const Segs &segs, int cols, int cursorCol = -1);

// Packed relay::CellColor: the high byte is the kind (1 palette, 2 RGB), the low 24 bits the value.
// Invalid means "the terminal's default". The first 16 palette entries are the theme's own, as on
// the desktop; the cube and the greys are xterm's, as in app/screen.js.
QColor colorOf(quint32 packed, const QVector<QColor> &ansi16);

// A key as the bytes a terminal expects; empty for a key this view does not send. The same table
// as app/screen.js keyEventBytes(), with Qt's key codes.
QByteArray keyBytes(int key, Qt::KeyboardModifiers modifiers, const QString &text);

// ----- the screen and its scrollback (app/screen.js, ported without the DOM) --------------------
//
// One column: the history rows, then the live block. It must stay contiguous — the row after the
// last history row is the frame's `base` — so output arriving while somebody reads further up is
// fetched rather than skipped. Scroll positions are in rows; the widget passes its own in.
class ScreenModel {
public:
    static constexpr int kPage = 80;        // rows per history request (the protocol caps 200)
    static constexpr int kMax = 2000;       // history rows kept
    static constexpr int kPrefetch = 30;    // rows of warning before the top or the seam

    struct Cursor { int row = 0, col = 0; bool visible = true; };
    struct HistoryRow { qint64 row = 0; Segs segs; };

    // What one frame changed.
    struct Applied {
        bool snapshot = false;
        bool broke = false;          // geometry or the alternate screen changed: history dropped
        QVector<int> rows;           // live rows that changed (all of them on a snapshot)
    };
    Applied apply(const QJsonObject &frame);

    // One `history` reply. `added` is how many rows went in, `top` whether above what was held.
    struct Inserted { int added = 0; int removed = 0; bool top = false; };
    Inserted applyHistory(const QJsonObject &reply);

    // The next page to ask for, or none. `firstVisible` is the top visible row of the column
    // (history rows count from 0), `visible` how many rows fit on screen.
    struct Request { bool want = false; qint64 before = -1; int count = 0; };
    Request nextRequest(int firstVisible, int visible);
    // Drop the oldest rows beyond kMax that lie wholly above `firstVisible - kPrefetch`; returns
    // how many went, so the view can keep the reader on their line.
    int trim(int firstVisible);

    void resetHistory();
    qint64 historyBottom() const;   // one past the newest history row, -1 when none are held
    qint64 gapRows() const;
    bool nearSeam(int firstVisible, int visible) const;

    // Everything painted: history rows, then `rows` live rows.
    int columnRows() const { return int(m_history.size()) + m_rows; }
    // The cells of column row `index`, the cursor included on the live block.
    QVector<Cell> cellsAt(int index) const;

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    bool alt() const { return m_alt; }
    Cursor cursor() const { return m_cursor; }
    qint64 liveBase() const { return m_liveBase; }
    qint64 historyTop() const { return m_historyTop; }
    int historySize() const { return int(m_history.size()); }
    bool more() const { return m_more; }
    bool pending() const { return m_pending; }
    void setPending(bool pending) { m_pending = pending; }
    void setMore(bool more) { m_more = more; }
    Segs liveLine(int row) const { return m_lines.value(row); }
    bool hasFrame() const { return m_hasFrame; }

private:
    void checkSeam();
    int m_rows = 24, m_cols = 80;
    bool m_alt = false;
    bool m_hasFrame = false;
    Cursor m_cursor;
    QHash<int, Segs> m_lines;
    QVector<HistoryRow> m_history;
    qint64 m_historyTop = -1;       // absolute row of m_history[0]; -1 when nothing is held
    qint64 m_liveBase = -1;         // the frame's `base`; -1 until one says
    bool m_more = true;
    bool m_pending = false;
};

// The row actions this client has words for, in the order the phone's sheet lists them, filtered
// to the ones the row itself lists. A row's `actions` are the whole truth about it.
QStringList offeredActions(const QJsonObject &row);
QString actionWords(const QString &action, const QString &kind);

} // namespace remoteview

// ----- the terminal area --------------------------------------------------------------------------
class RemoteScreen final : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit RemoteScreen(QWidget *parent = nullptr);

    remoteview::ScreenModel &model() { return m_model; }
    void apply(const QJsonObject &frame);
    void applyHistory(const QJsonObject &reply);
    // A refused page: stop waiting, and stop asking until the reader scrolls again.
    void historyFailed();
    void setHistoryEnabled(bool on);
    void setDriving(bool on) { m_driving = on; }
    void toLive();
    bool atBottom() const;
    void requestIfNeeded();
    void applyTheme();
    QString selectedText() const;
    QSize sizeHint() const override;

signals:
    void historyWanted(qint64 before, int count);   // before < 0: the newest page
    void keys(const QByteArray &bytes);
    void paste(const QString &text);
    void typedWhileWatching();
    void behindChanged(bool behind);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    bool focusNextPrevChild(bool) override { return false; }   // Tab goes to the program

private:
    void fit();
    void updateRange(bool keepBottom);
    int firstVisible() const;
    int visibleRows() const;
    QPoint cellAt(const QPoint &pos) const;   // (col, column row)
    void setBehind(bool behind);

    remoteview::ScreenModel m_model;
    QFont m_baseFont, m_font;
    qreal m_cellW = 8, m_cellH = 16, m_ascent = 12;
    int m_lineSpacing = 0, m_margin = 6;
    QColor m_fg, m_bg, m_cursor;
    QVector<QColor> m_ansi;
    bool m_historyEnabled = false;
    bool m_driving = false;
    bool m_behind = false;
    bool m_selecting = false;
    QPoint m_selStart{-1, -1}, m_selEnd{-1, -1};
};

// ----- the pane --------------------------------------------------------------------------------
class RemotePane final : public QWidget, public PaneView {
    Q_OBJECT
public:
    // Every client → desktop message leaves through here, already carrying `pane`.
    using Sink = std::function<void(const QJsonObject &)>;
    RemotePane(const QString &paneId, const QString &title, const QString &desktop, Sink sink,
               QWidget *parent = nullptr);
    ~RemotePane() override;

    // A pane opened through a viewer sidecar: wired to it, and closed on it when it goes. Without
    // `viewer` it is the app's own device session, RemoteViewer::instance().
    static RemotePane *openFromViewer(const QString &paneId, const QString &title,
                                      const QString &desktop);
    static RemotePane *openFromViewer(const QString &paneId, const QString &title,
                                      const QString &desktop, RemoteViewer &viewer);

    // One server message, verbatim. Anything for another pane is ignored.
    void handle(const QJsonObject &message);
    // `guest` is somebody else's share joined with a code: then setRole() says what the pane offers.
    void setCapability(const QString &capability, const QStringList &features);
    bool guest() const { return m_capability == QLatin1String("guest"); }
    // A guest's role, `viewer` or `editor`. A viewer watches; an editor may ask to type and may
    // send prompts, which wait for the owner's approval.
    void setRole(const QString &role);
    QString role() const { return m_role; }
    // This guest's participant id (the guest viewer's `welcome.participant`): how a guest knows
    // the keyboard is theirs, as a device knows it by its device id.
    void setParticipant(const QString &id) { if (!id.isEmpty()) m_participant = id; }
    // The owner ended this guest's access, or the pane left what is shared with them: the reason
    // stays on the pane, and nothing on it sends anything any more.
    void markEnded(const QString &message);
    bool ended() const { return m_ended; }
    QString driveText() const;
    // The sidecar's own state: offline and reconnecting are said on the pane, and a reconnect asks
    // for a fresh screen and state.
    void setConnection(const QString &state, const QString &message);

    // PaneView
    QString paneTitle() const override;
    void focusView() override;
    void setHeaderRightInset(int pixels) override;
    std::function<void()> onTitleChanged;
    std::function<void()> onClosed;        // the pane is going: close it on the sidecar

    QString paneId() const { return m_pane; }
    bool driving() const { return m_driving; }
    QString myDevice() const { return m_myDevice; }
    // This device's id on the desktop (the viewer's `welcome.device`). Without it the pane learns
    // it from the handoff that answers its own control_request.
    void setMyDevice(const QString &device) { if (!device.isEmpty()) m_myDevice = device; }
    void showNote(const QString &text, int milliseconds = 5000);
    // For tests and for the palette: what the row's × and context menu offer.
    bool rowHasRemoveButton(const QString &rowId) const;
    QStringList rowMenuActions(const QString &rowId) const;
    void triggerRowAction(const QString &rowId, const QString &action);
    QStringList modelMenuLabels() const;
    void pickModel(int index);
    QStringList conversationMenuLabels() const;
    void triggerConversation(int index);   // 0 is "New conversation" when it is offered
    QPlainTextEdit *promptBox() const { return m_box; }
    RemoteScreen *screen() const { return m_screen; }
    QString noteText() const;
    void requestControl();
    void releaseControl();
    void sendPrompt(const QString &when = QString());

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void send(QJsonObject message);
    void updateState(const QJsonObject &state);
    void renderThinking();
    void renderQueue();
    void renderStrip();
    void applyGuestUi();
    void onControl(const QJsonObject &message);
    void onGuestMessage(const QString &kind, const QJsonObject &message, bool mine);
    void updateDriveUi();
    void hint(const QString &id, const QString &text);
    void rowMenu(const QString &rowId, const QPoint &globalPos);
    void enter();
    void compose(const QString &text, const QString &when);
    void setTail(const QString &text);
    bool busy() const;
    QJsonObject rowById(const QString &id) const;
    QJsonArray rowList() const;

    QString m_pane, m_title, m_desktop;
    Sink m_sink;
    QString m_capability = QStringLiteral("view");
    QStringList m_features;
    QJsonObject m_state;
    qint64 m_lastSeq = -1;
    int m_historySeq = 0;
    QString m_historyRequest;
    // control (section 10.3), followed exactly as app/app.js follows it
    bool m_driving = false;
    bool m_claiming = false;          // a control_request is out; the next device id is ours
    QString m_holder = QStringLiteral("owner"), m_holderName, m_holderDevice, m_myDevice;
    QJsonArray m_presence;
    // the three-step Enter of the desktop's prompt box (pane.js `staged`)
    struct Staged { QString text; int stage = 0; qint64 at = 0; QString rowId, steerId; QSet<QString> known; };
    Staged m_staged;
    bool m_thinkingHidden = false, m_thinkingExpanded = false;
    QString m_pendingTail;
    bool m_hasPendingTail = false;
    QString m_typedAhead;             // keys typed on a row, for when its text comes back
    bool m_connected = true, m_everConnected = true;
    int m_noteSerial = 0;
    // a guest (section 10): who this pane's participant is, and what the owner has allowed
    QString m_role = QStringLiteral("viewer");
    QString m_participant;            // learnt from `welcome` or the `you` row of `participants`
    bool m_asking = false;            // a control_request is waiting for the owner
    int m_askSerial = 0;
    bool m_paused = false;
    QString m_pauseReason;
    bool m_toldApproval = false;      // the "your prompts wait" note is said once
    bool m_ended = false;
    QString m_endedMessage;

    RemoteScreen *m_screen = nullptr;
    QFrame *m_driveBar = nullptr;
    QLabel *m_driveLabel = nullptr, *m_presenceLabel = nullptr;
    QPushButton *m_take = nullptr, *m_release = nullptr, *m_live = nullptr;
    QLabel *m_note = nullptr;
    QFrame *m_thinking = nullptr;
    QLabel *m_thinkingHeader = nullptr;
    QToolButton *m_thinkingToggle = nullptr, *m_thinkingClose = nullptr;
    QPlainTextEdit *m_thinkingTail = nullptr;
    QFrame *m_queue = nullptr;
    QLabel *m_queueTitle = nullptr, *m_queueHint = nullptr, *m_queueReason = nullptr, *m_running = nullptr;
    QListWidget *m_rows = nullptr;
    QFrame *m_composer = nullptr;
    QPlainTextEdit *m_box = nullptr;
    QLabel *m_mode = nullptr, *m_folder = nullptr, *m_clock = nullptr, *m_context = nullptr;
    QLabel *m_allowance = nullptr;   // the Relay Free chip, next to the context one
    QToolButton *m_model = nullptr, *m_sessions = nullptr, *m_sendMenu = nullptr;
    QPushButton *m_send = nullptr;
    QMenu *m_modelMenu = nullptr, *m_sessionsMenu = nullptr;
    int m_rightInset = 0;
};

// ----- the sidecar ------------------------------------------------------------------------------
// Two per process at most: instance() holds the encrypted session to this Relay's own desktop as
// one of its devices, guest() the session to somebody else's share joined with a meeting code.
// Each holds every pane opened from it.
class RemoteViewer final : public QObject {
    Q_OBJECT
public:
    static RemoteViewer &instance();
    static RemoteViewer &guest();
    bool isGuest() const { return m_guest; }

    // Start `python3 -m remote.viewer` (with `--guest` for guest()) if it is not running.
    // RELAY_REMOTE_VIEWER names a script to run instead (the test harness's fake viewer); it is
    // passed `--guest` the same way.
    bool ensure(QString *error);
    void send(const QJsonObject &message);
    void sendToDesktop(const QJsonObject &message) { send({{"t", "send"}, {"message", message}}); }
    // Stop the sidecar once nothing needs it: no pane open from it and no dialog up.
    void release();
    void stop();
    bool running() const;

    QString state() const { return m_state; }
    QString desktop() const { return m_desktop; }
    QString device() const { return m_device; }
    QString capability() const { return m_capability; }
    QStringList features() const { return m_features; }
    QJsonArray panes() const { return m_panes; }
    // A guest's role (`viewer` or `editor`) and when its access runs out (epoch seconds, 0 unknown).
    QString role() const { return m_role; }
    QString participant() const { return m_participant; }
    qint64 expires() const { return m_expires; }
    int openPanes() const { return m_open; }
    void paneOpened(const QString &paneId);
    void paneClosed(const QString &paneId);
    void setDialogUp(bool up) { m_dialogUp = up; }

signals:
    void status(const QString &state, const QString &message);
    void code(const QString &code);
    void paired(const QString &desktop, const QString &fingerprint);
    void welcome(const QString &capability, const QStringList &features);
    void message(const QJsonObject &message);
    void panesChanged(const QJsonArray &items);
    // `reason` is the guest viewer's word for a refused join (wrong_pin, burned, expired,
    // no_such_code, not_admitted, rate_limited, closed, internal); empty otherwise.
    void failed(const QString &message, const QString &reason);
    // Guest only: admitted to `desktop`'s share with `role`, able to see `panes`.
    void joined(const QString &desktop, const QString &role, const QStringList &panes);
    // Guest only: the owner ended this guest's access, and the record is gone.
    void ended(const QString &message);

private:
    explicit RemoteViewer(bool guest);
    ~RemoteViewer() override;
    void onReadable();
    void handle(const QJsonObject &line);

    bool m_guest = false;
    QProcess *m_process = nullptr;
    QByteArray m_pending;
    QString m_state = QStringLiteral("offline");
    QString m_desktop, m_device, m_capability = QStringLiteral("view");
    QStringList m_features;
    QJsonArray m_panes;
    QString m_role, m_participant;
    qint64 m_expires = 0;
    QSet<QString> m_openIds;
    int m_open = 0;
    bool m_dialogUp = false;
};

// ----- a joined share -----------------------------------------------------------------------------
// What keeps a guest's session going after the join dialog has closed. The share's scope can change
// under a guest — a tab shared whole grows and shrinks as panes open and close in it — and every
// `panes` list the viewer passes on is already cut down to it: a pane new to that list is opened
// and placed, a pane gone from it is marked ended on screen. One at a time; a new join replaces it.
class GuestSession final : public QObject {
    Q_OBJECT
public:
    // `first` is true for the first pane placed by a join, false for every pane after it.
    using Place = std::function<void(RemotePane *pane, bool first)>;
    // Opens every pane in `panes` on RemoteViewer::guest() and places each; returns the session.
    static GuestSession *start(const QStringList &panes, Place place);
    static GuestSession *current();
    // The panes this session opened that are still open.
    QStringList openIds() const;

private:
    explicit GuestSession(Place place);
    void sync(const QJsonArray &items);
    void openPane(const QString &id);

    Place m_place;
    QHash<QString, QPointer<RemotePane>> m_panes;
    QSet<QString> m_known;   // in scope when last told: a pane the person closed is not reopened
    bool m_placed = false;   // the first pane placed is `first`
};

// ----- "Open a shared pane…" ----------------------------------------------------------------------
// Paste the pairing link, compare the code, pick a pane. With a paired record already on this
// machine it goes straight to the pane list. `place` puts the new pane into the window.
class RemotePaneDialog final : public QDialog {
    Q_OBJECT
public:
    using Place = std::function<void(RemotePane *pane)>;
    explicit RemotePaneDialog(Place place, QWidget *parent = nullptr);
    ~RemotePaneDialog() override;

    static void open(QWidget *parent, Place place);

private:
    void showStatus(const QString &state, const QString &message);
    void showPanes(const QJsonArray &items);
    void pair();
    void openSelected();

    Place m_place;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_pairPage = nullptr, *m_codePage = nullptr, *m_panesPage = nullptr, *m_waitPage = nullptr;
    QLineEdit *m_link = nullptr;
    QPushButton *m_pairButton = nullptr;
    QLabel *m_code = nullptr, *m_codeNote = nullptr, *m_wait = nullptr, *m_desktopLabel = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_open = nullptr, *m_forget = nullptr;
    QLabel *m_status = nullptr;
    bool m_paired = false;
};

// ----- "Join a shared session…" -------------------------------------------------------------------
// Somebody shares a pane or a tab and reads out a meeting code and a PIN (`/join BQRT`). Page one
// takes the code, the PIN, the name the owner will see and — behind "Server…", since almost nobody
// changes it — the rendezvous; page two waits for the owner to let this person in, showing the
// check code the owner sees beside the knock. Once admitted, every pane in the guest's scope is
// placed through `place` and a GuestSession keeps the share's later changes coming.
class JoinDialog final : public QDialog {
    Q_OBJECT
public:
    using Place = GuestSession::Place;
    JoinDialog(const QString &code, Place place, QWidget *parent = nullptr);
    ~JoinDialog() override;

    static void open(QWidget *parent, const QString &code, std::function<void(RemotePane *pane, bool first)> place);
    static constexpr const char *kDefaultServer = "https://join.relay-terminal.ai";

public slots:
    void reject() override;

private:
    void showStatus(const QString &state, const QString &message);
    void showError(const QString &message, const QString &reason);
    void onJoined(const QString &desktop, const QString &role, const QStringList &panes);
    void updateJoinButton();
    void join();
    void backToForm(const QString &message);
    bool waiting() const;

    Place m_place;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_formPage = nullptr, *m_waitPage = nullptr;
    QLineEdit *m_code = nullptr, *m_pin = nullptr, *m_name = nullptr, *m_server = nullptr;
    QToolButton *m_serverToggle = nullptr;
    QLabel *m_error = nullptr, *m_wait = nullptr, *m_check = nullptr;
    QPushButton *m_join = nullptr, *m_cancel = nullptr;
    bool m_done = false;
    bool m_rejoin = false;   // a `connect` for a stored record is out: its progress is shown here
};

} // namespace relay
