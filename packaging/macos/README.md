# Relay for macOS — native beta

Choose the **Apple Silicon (arm64)** disk image for M-series Macs, or **Intel (x64)** for Intel Macs. macOS 13 Ventura or newer is required. Open the disk image and drag Relay into Applications.

These beta builds are **ad-hoc signed, not Apple Developer ID signed or notarized**. Gatekeeper may block a downloaded copy. After attempting to open your trusted download, use System Settings → Privacy & Security → Open Anyway. See [Apple's instructions](https://support.apple.com/en-us/102445). Do not disable Gatekeeper globally.

The application contains Qt, Python 3.13 with its dependencies, and GNU Bash 5.3.20. It does not require Homebrew, a separate Python installation or Rosetta. Relay uses Bash for its integrated terminal and agent commands; it does not change your default login shell.

To uninstall, move Relay.app to the Trash. This does not remove your saved settings, conversations or Keychain credentials.

## Building and verification

`.github/workflows/macos.yml` builds on native Apple Silicon and Intel runners. Both architectures run storage, Keychain, worker and PTY tests. Packaging pins runtime archives by SHA256 in `runtime-pins.json`; `build-bash.sh` applies all 20 official Bash 5.3 patches before compiling against the macOS SDK.

`package.sh VERSION` installs the CMake bundle, adds the private runtimes, deploys Qt frameworks/plugins with `macdeployqt`, audits all Mach-O dependencies, ad-hoc signs the bundle, and creates a DMG. `installed-smoke.py` mounts the disk image, copies the app to a new path containing spaces, ejects the image, and verifies:

- Private runtime imports with system-only PATH.
- Native PTY command loading, hash acknowledgement, Unicode output and exit status.
- Real GUI startup with its bundled Bash and Python worker, followed by normal shutdown.
- Bundle signatures remain valid after running.

The bundled Bash corresponding source, patches, license and exact build script are included under `Contents/Resources/bash/share/relay-source`. Other license notices are under `Contents/Resources/licenses` and the Python runtime. Relay's matching source is linked from the release notes.

Qt deployment reference: https://doc.qt.io/qt-6.8/macos-deployment.html
Python runtime format: https://gregoryszorc.com/docs/python-build-standalone/stable/distributions.html
