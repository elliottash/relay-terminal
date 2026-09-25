// Card #234Z: Alt+Esc leaves an ssh or mosh session. A session client forwards a Ctrl+C to the
// far side, so #H2KQ's two-press rule could never end it — the first press only interrupts the
// remote command. The session-exit path terminates the client's process group on the first
// press instead. Shell cases use a real pty, like the H2KQ cases whose helpers this reuses.
// Run alone: relay-consolemode-tests --234z-only.

namespace cases {

QString z234zToast(Pane &pane)
{
    QLabel *toast = pane.findChild<QLabel *>(QStringLiteral("toast"));
    return toast ? toast->text() : QString();
}

void z234zCases()
{
    // ----- an ssh-named program: the first Alt+Esc leaves the "session" outright ----------------
    {
        QTemporaryDir homeDir;
        XcxdShellContext ctx;
        ctx.workspace = homeDir.path();
        Pane pane(ctx.workspace, ctx.workspace, true, relay::defaultEngineCore(), &ctx);
        pane.show();
        pane.resize(900, 650);
        xcxdPump(50);
        // argv[0] says ssh — what the pane sees while a session client runs — and SIGINT is
        // ignored, the way a real client only passes a ^C through to the far side: no polite
        // first press can end this program, only the session-exit path can.
        h2kqRun(pane, QStringLiteral("bash -c 'trap \"\" INT; exec -a ssh sleep 30'"), "ssh-exit");
        // The busy line names the key that leaves the session, from the first beat of it.
        QString busy;
        for (int i = 0; i < 200
             && !(busy = h2kqBusyText(pane)).contains(QStringLiteral("Alt+Esc exits")); ++i)
            xcxdPump(25);
        CHECK(busy.contains(QStringLiteral("Alt+Esc exits")));
        // One press leaves the session: the client is terminated, not interrupted. The toast
        // waits its turn behind whatever hint is up, so it is polled for, not sampled once.
        h2kqKey(pane, Qt::Key_Escape, Qt::AltModifier);
        QString exited;
        for (int i = 0; i < 80 && !(exited = z234zToast(pane)).contains(QStringLiteral("Exited ssh")); ++i)
            xcxdPump(25);
        CHECK(exited.contains(QStringLiteral("Exited ssh")));
        for (int i = 0; i < 200 && !h2kqStripStopGone(pane); ++i)
            xcxdPump(25);
        CHECK(h2kqStripStopGone(pane));
        for (int i = 0; i < 200 && !h2kqStripStopGone(pane); ++i)
            xcxdPump(25);
        CHECK(h2kqStripStopGone(pane));
    }

    // ----- a program that is no session client keeps #H2KQ's two presses -----------------------
    {
        QTemporaryDir homeDir;
        XcxdShellContext ctx;
        ctx.workspace = homeDir.path();
        Pane pane(ctx.workspace, ctx.workspace, true, relay::defaultEngineCore(), &ctx);
        pane.show();
        pane.resize(900, 650);
        xcxdPump(50);
        // Same INT-ignoring program, but argv[0] says bash: Alt+Esc stays a two-press stop. The
        // marker in the command line makes the liveness probe target this case's program alone —
        // other panes and agents on this machine legitimately run their own `sleep 30` loops.
        h2kqRun(pane, QStringLiteral("bash -c 'trap \"\" INT; sleep 30 # 234z-two-press'"), "two-press");
        // The stop strip appears with the submit; the pane's own sight of the program comes a
        // poll later, and a press before it is a key into a pane that has nothing to stop.
        QString busy;
        for (int i = 0; i < 200 && !(busy = h2kqBusyText(pane)).contains(QStringLiteral("stops")); ++i)
            xcxdPump(25);
        CHECK(busy.contains(QStringLiteral("stops")));
        h2kqKey(pane, Qt::Key_Escape, Qt::AltModifier);   // the polite press: Ctrl+C, survived
        xcxdPump(300);
        CHECK(!h2kqStripStopGone(pane));
        xcxdPump(450);                                   // past the beat, the next press closes it
        h2kqKey(pane, Qt::Key_Escape, Qt::AltModifier);
        // The kill is the fact under test: the program that ignored Ctrl+C is dead. The pane's
        // own return to the prompt is case 1's business — asserting the strip here would only
        // measure how fast the harness's shell poll lands.
        int dead = 0;
        for (int i = 0; i < 200; i++) {
            xcxdPump(25);
            if (QProcess::execute(QStringLiteral("/bin/sh"), {QStringLiteral("-c"),
                    QStringLiteral("pgrep -f '[s]leep 30 # 234z-two-press' >/dev/null || exit 0; exit 1")}) == 0) {
                dead = 1;
                break;
            }
        }
        CHECK(dead);
    }
}

} // namespace cases
