# Staging notes — #Z82M Try-it

- Everything between the pane and the picture is **shipped code at `main`** (`ccba73c9` +
  `69390828`): the desktop media tool, the hosted session and its registration proof, the
  gateway's `/v1/images` route with its image-count quota, the `hosted_quota` event and the
  chip. The binary is built from a clean `git archive main` export (`/tmp/z82m-verify`),
  not from the shared checkout (whose tree holds another session's half-finished C++).
- **What is faked**: `openrouter.ai` — replaced by `fake_upstream.py`, a scripted provider
  that (a) answers the first chat turn with a `media_generate` tool call for a lighthouse
  and (b) returns a drawn PNG for `/images`. The model's decision to call the tool is
  scripted because no real model is reachable from a keyless stage; once the tool is
  called, nothing else is.
- The gateway allows 5 images/day for the stage, so the chip has room to count down and
  the person is never blocked.
- Fresh profile: `XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`HOME` under the sandbox, keyring off,
  so the install is genuinely keyless; `RELAY_HOSTED_URL` points Relay Free at the local
  gateway (loopback is explicitly allowed by the staged config).
- Run `stage.sh` to (re)create the sandbox; it prints the exact line to open Relay with.
  `./stage.sh` starts the fake upstream (port 4761) and the gateway (4762); both write
  logs under the sandbox. Kill with the pids it leaves in the sandbox.
- The mechanical pass (`tryit-mechanical.md`, screenshots `0*.png`) was run by the
  implementing session before handing over; the person's pass is the one that counts.
