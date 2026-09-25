"""IPython startup hook for Relay's OSC 133 command boundaries.

Load from IPython's startup directory. The prompt emits A/B; execution hooks emit
C/D, matching shell/relay-integration.bash. The status is 0 for success and 1
for a Python exception (IPython does not expose a shell-style numeric exit code).
"""
from IPython.terminal.prompts import Prompts, Token

_OSC = "\x1b]133;{}\x07"


class RelayPrompts(Prompts):
    def in_prompt_tokens(self, cli=None):
        return [(Token.Prompt, _OSC.format("A")),
                *super().in_prompt_tokens(cli),
                (Token.Prompt, _OSC.format("B"))]


_shell = get_ipython()  # IPython injects this into startup files.
_shell.prompts = RelayPrompts(_shell)


def _before_cell(info):
    print(_OSC.format("C"), end="", flush=True)


def _after_cell(result):
    print(_OSC.format("D;1" if result.error_before_exec or result.error_in_exec else "D;0"),
          end="", flush=True)


_shell.events.register("pre_run_cell", _before_cell)
_shell.events.register("post_run_cell", _after_cell)
