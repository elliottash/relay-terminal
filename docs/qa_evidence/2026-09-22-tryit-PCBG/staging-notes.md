# Staging notes — #PCBG Try it

This is a real build of Relay (`build/relay` from this checkout, at the commit that landed the
card) running with a throwaway profile under `/tmp/claude-1000/tryit/pcbg`: its own HOME, config,
runtime and temp dirs, so none of your settings, sessions or keyring are touched. The workspace is
an empty invented directory. The "active job" is just `sleep 600 &` started by the pane's shell
wrapper before it execs bash — it produces no output while it runs, unlike a real build. What is
genuinely the shipped behaviour: the close dialog, its default (Cancel), backgrounding into a
hidden window, and Sessions → Background reopening the same live pane. Background sessions end
when that Relay process exits; closing the staged window (or quitting it) ends them.
