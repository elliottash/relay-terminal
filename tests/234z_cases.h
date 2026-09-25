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
        const harness::ProcessGuard guard(pane);   // #DSKT: kill the shell's groups at case end
        pane.show();
        pane.resize(900, 650);
        // argv[0] says ssh — what the pane sees while a session client runs — and SIGINT is
        // ignored, the way a real client only passes a ^C through to the far side: no polite
        // first press can end this program, only the session-exit path can.
        h2kqRun(pane, QStringLiteral("bash -c 'trap \"\" INT; exec -a ssh sleep 30'"), "ssh-exit");
        // The busy line names the key that leaves the session, from the first beat of it.
        CHECK(waitForBusyText(pane, QStringLiteral("Alt+Esc exits")));
        // One press leaves the session: the client is terminated, not interrupted. The toast
        // queues behind whatever hint is up for ~1 s, so it is waited on, not sampled once.
        h2kqKey(pane, Qt::Key_Escape, Qt::AltModifier);
        CHECK(waitForToast(pane, QStringLiteral("Exited ssh")));
        CHECK(waitForStripEmpty(pane));
    }

    // ----- a program that is no session client keeps #H2KQ's two presses -----------------------
    {
        QTemporaryDir homeDir;
        XcxdShellContext ctx;
        ctx.workspace = homeDir.path();
        Pane pane(ctx.workspace, ctx.workspace, true, relay::defaultEngineCore(), &ctx);
        const harness::ProcessGuard guard(pane);   // #DSKT: kill the shell's groups at case end
        pane.show();
        pane.resize(900, 650);
        // Same INT-ignoring program, but argv[0] says bash: Alt+Esc stays a two-press stop. The
        // program is never found by name: its id is the pty's foreground process group (#234Z),
        // read from the pane, so other panes' and agents' own `sleep 30` loops on this machine
        // cannot make this case pass or fail (card #DSKT).
        h2kqRun(pane, QStringLiteral("bash -c 'trap \"\" INT; sleep 30'"), "two-press");
        // The stop strip appears with the submit; the pane's own sight of the program comes a
        // poll later, and a press before it is a key into a pane that has nothing to stop. And
        // the busy line must name sleep itself: a press that lands while bash is still starting
        // the command — busy, but the `trap "" INT` not installed yet — kills it outright,
        // which is the race behind this case's flakes (card #8ABD).
        CHECK(waitForBusyText(pane, QStringLiteral("sleep")));
        CHECK(waitForProcessBusy(pane, true));
        // The kill below targets this group: the tty's foreground process group, one poll beat
        // after the program started (card #DSKT). Non-interactive bash runs sleep unexec'd in
        // its own foreground group, so this one id names the program and its parent alike.
        int group = 0;
        CHECK(waitUntil(
            [&] {
                group = pane.foregroundProcessGroup();
                return group > 0;
            },
            QStringLiteral("foreground process group of the two-press program")));
        h2kqKey(pane, Qt::Key_Escape, Qt::AltModifier);   // the polite press: Ctrl+C, survived
        // The fact under test a beat later: the program that ignored the ^C is still the pane's
        // foreground. The strip's stop control is layout — it can hide for a moment behind the
        // toast that the press raises — so it is not the thing to assert (card #8ABD, race 3).
        // The wait is one 650 ms beat: it both lets a would-be death show and passes the 600 ms
        // mark that arms the next press as the kill.
        xcxdPump(650);
        CHECK(pane.processBusy());
        h2kqKey(pane, Qt::Key_Escape, Qt::AltModifier);   // past the beat: this press closes it
        // The kill is the fact under test: the program that ignored Ctrl+C is dead. Signalling
        // its group fails with ESRCH once the group is empty — no process is found by name, and
        // a zombie is gone the moment its parent reaps it, within this wait (card #DSKT).
        CHECK(waitUntil(
            [group] { return ::kill(pid_t(-group), 0) != 0; },
            QStringLiteral("kill of the two-press program")));
    }
}

} // namespace cases
