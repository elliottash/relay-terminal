# Isolated conditional zsh redraw trial — PASS

No repository shell script changed. zsh-trial.py copies shell/ into its isolated fixture and changes ONLY the widget to:

```sh
__relay_r_redraw() {
  if [ -n "$BUFFER" ]; then
    __relay_r_rows=$(__relay_r_rows_for "$BUFFER")
  else
    printf "\n\n"
    zle reset-prompt
  fi
}
```

Drive exited 0 on GUI build7 / 11H.05. Visual comparison 03-zsh-composer-failure.png -> 04-zsh-native-failure.png: inline no-provider message is preserved after Ctrl+H, the prompt immediately after that message is not duplicated, and the native command prints normally and returns one prompt. The old prompt before the inline message remains in scrollback, while a fresh prompt appears after the inline message. This is expected redraw placement and fixes the previous disappearing-message/two-adjacent-prompts artifact in this reproduction.

Trial covers composer success/failure, inline message, switching to native control and native failure. It does not establish all zsh custom prompt layouts or fullscreen interactions. Parent can apply this exact conditional widget in implementation if desired; verifier changed only its fixture.
