# Add `--verbose` to `hello.py`

## Goal

Update `hello.py` so `python3 hello.py --verbose` still prints the normal greeting and additionally prints the running Python version. The default behavior (`python3 hello.py`) must remain unchanged.

## Findings

- Program: `hello.py`
- Current content:

  ```python
  import sys


  def main():
      print("hello")


  if __name__ == "__main__":
      main()
  ```

- `sys` is already imported but is currently used only implicitly as the executable entry point; it is not used in `main()`.
- Project instruction in `AGENTS.md` requires always calling `hello.py`; it was run successfully and currently prints `hello`.

## Implementation steps

1. Edit `hello.py`.
2. Import `argparse` alongside `sys`.
3. In `main()`, create an argument parser:

   ```python
   parser = argparse.ArgumentParser(description="Print a greeting.")
   parser.add_argument(
       "--verbose",
       action="store_true",
       help="also print the Python version",
   )
   args = parser.parse_args()
   ```

4. Keep the existing greeting:

   ```python
   print("hello")
   ```

5. If `args.verbose` is true, print the Python version:

   ```python
   if args.verbose:
       print(sys.version)
   ```

6. Ensure importing `hello.py` does not parse command-line arguments or print output; argument handling must remain inside `main()`.

## Expected result

The complete program should be equivalent to:

```python
import argparse
import sys


def main():
    parser = argparse.ArgumentParser(description="Print a greeting.")
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="also print the Python version",
    )
    args = parser.parse_args()

    print("hello")

    if args.verbose:
        print(sys.version)


if __name__ == "__main__":
    main()
```

## Risks

- Calling `parser.parse_args()` at module import time would break imports and tests; keep it inside `main()`.
- Printing `sys.version` rather than `sys.version_info` provides the full Python version and build details, as requested.
- No existing command-line interface is specified, so adding `argparse` should be backward-compatible with the current invocation.

## Verification

Run these commands after implementation:

```bash
python3 hello.py
python3 hello.py --verbose
python3 hello.py --help
```

Expected:

1. The first command prints exactly `hello`.
2. The second prints `hello` followed by the current Python version.
3. The third documents `--verbose` without unexpected errors.
