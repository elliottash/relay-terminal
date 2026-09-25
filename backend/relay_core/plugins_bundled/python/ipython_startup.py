"""Relay's OSC 133 command boundaries for a Python console pane (#83YV).

One file, two hosts, both started by the pane with this file named on the command line:

- `jupyter console --existing <file> --config=ipython_startup.py`: jupyter console loads it as a
  traitlets config file (`get_config` is in scope), and it patches the ZMQ terminal shell.
- `ipython --InteractiveShellApp.exec_files=[ipython_startup.py]`: IPython runs it in the user
  namespace (`get_ipython` is in scope), and it patches that shell.

The prompt carries A (prompt start) and B (input start) as prompt_toolkit zero-width escapes, so
they reach the terminal without taking a column; a cell writes C before it runs and D;<status>
after, 0 for success and 1 for an exception, matching shell/integration.bash. Everything this
file defines in IPython's namespace starts with `_relay`, so `%who` stays the user's.
"""

_RELAY_OSC = "\x1b]133;{}\x07"


def _relay_wrap_message(message):
    """The prompt `message` with A before it and B after, whatever form the host gave it in."""
    from prompt_toolkit.formatted_text import to_formatted_text

    def wrapped():
        shown = message() if callable(message) else message
        return [("[ZeroWidthEscape]", _RELAY_OSC.format("A")), *to_formatted_text(shown),
                ("[ZeroWidthEscape]", _RELAY_OSC.format("B"))]
    return wrapped


def _relay_mark(code):
    import sys
    sys.stdout.write(_RELAY_OSC.format(code))
    sys.stdout.flush()


def _relay_patch_jupyter_console(config):
    """jupyter console: A/B through the prompt session, C/D around each cell, the agent's output
    shown in the pty (other clients of the same kernel are the agent's py_run_cell)."""
    from jupyter_console.ptshell import ZMQTerminalInteractiveShell as Shell

    config.ZMQTerminalInteractiveShell.include_other_output = True
    config.ZMQTerminalInteractiveShell.other_output_prefix = "[agent] "
    config.ZMQConsoleApp.confirm_exit = False
    config.ZMQTerminalIPythonApp.confirm_exit = False
    if getattr(Shell, "_relay_marks", False):
        return
    Shell._relay_marks = True
    init_cli, run_cell = Shell.init_prompt_toolkit_cli, Shell.run_cell

    def init_prompt_toolkit_cli(self):
        init_cli(self)
        self.pt_cli.message = _relay_wrap_message(self.pt_cli.message)

    def spy_on_replies(self):
        """The execute_reply's status is read inside the shell and dropped; the shell channel's
        `get_msg` coroutine is where it passes by, so a wrapper there keeps the last one."""
        import inspect
        channel = self.client.shell_channel
        if getattr(channel, "_relay_spy", False):
            return
        get_msg = channel.get_msg

        async def spied(*args, **kwargs):
            # A blocking client's channel answers at once, an async one with a coroutine; the
            # console's run_sync takes either.
            message = get_msg(*args, **kwargs)
            if inspect.isawaitable(message):
                message = await message
            if message.get("msg_type") == "execute_reply":
                self._relay_status = message.get("content", {}).get("status")
            return message

        channel.get_msg = spied
        channel._relay_spy = True

    def patched_run_cell(self, cell, store_history=True):
        if not cell or cell.isspace():
            return run_cell(self, cell, store_history)
        spy_on_replies(self)
        self._relay_status = None
        _relay_mark("C")
        try:
            return run_cell(self, cell, store_history)
        finally:
            _relay_mark("D;0" if self._relay_status == "ok" else "D;1")

    Shell.init_prompt_toolkit_cli = init_prompt_toolkit_cli
    Shell.run_cell = patched_run_cell

    # Ctrl+C in the pty. An attached console has no kernel manager, so jupyter console refuses
    # ("Cannot interrupt kernels we didn't start"); the Jupyter protocol's own interrupt is an
    # `interrupt_request` on the control channel, which ipykernel answers by interrupting the
    # running cell — whoever's it is, the person's or the agent's.
    from jupyter_console.app import ZMQTerminalIPythonApp as App
    handle_sigint = App.handle_sigint

    def interrupt(self, *args):
        client = self.kernel_client
        if self.shell._executing and not self.kernel_manager and getattr(client, "control_channel", None):
            client.control_channel.send(client.session.msg("interrupt_request", content={}))
            return
        return handle_sigint(self, *args)

    App.handle_sigint = interrupt
    # The kernel is Relay's worker's (the agent's py_* tools run in it): `exit()` in an attached
    # console leaves it running. Relay's kernel already answers exit() with keepkernel true
    # (py_kernel.KEEP_KERNEL_SOURCE); this covers a payload that says otherwise, so the console
    # never shuts the shared kernel down on its way out.
    Shell.keepkernel = property(lambda self: not self.own_kernel or self.__dict__.get("_relay_keep", False),
                                lambda self, value: self.__dict__.__setitem__("_relay_keep", value))


def _relay_patch_ipython(shell):
    """Plain IPython: the prompt options carry the message, the cell events carry C and D."""
    options = shell._extra_prompt_options

    def extra_prompt_options():
        result = options()
        result["message"] = _relay_wrap_message(result["message"])
        return result

    shell._extra_prompt_options = extra_prompt_options
    shell.events.register("pre_run_cell", lambda info: _relay_mark("C"))
    shell.events.register("post_run_cell", lambda result: _relay_mark(
        "D;1" if result.error_before_exec or result.error_in_exec else "D;0"))


if "get_config" in globals():                       # jupyter console, as its --config file
    _relay_patch_jupyter_console(get_config())      # noqa: F821 - traitlets puts it in scope
elif "get_ipython" in globals():                    # IPython, as a startup file
    _relay_patch_ipython(get_ipython())             # noqa: F821 - IPython puts it in scope
