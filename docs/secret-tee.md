# secret-tee

A `tee` replacement that automatically redacts environment variable values from output. Useful for logging or sharing terminal output without leaking API keys and secrets.

## Usage

```bash
source .env
out/scaffold 2>&1 | scripts/secret-tee                # redacted stdout
out/scaffold 2>&1 | scripts/secret-tee session.log     # redacted stdout + file
out/scaffold 2>&1 | scripts/secret-tee -a session.log  # append to file
```

Requires [uv](https://docs.astral.sh/uv/) — no Python install needed.

## Options

| Flag | Description |
|------|-------------|
| `-a`, `--append` | Append to output files instead of overwriting |
| `--min-length N` | Minimum env value length to redact (default: 8) |
| `-h`, `--help` | Show help |

## How it works

1. Reads all environment variable values that are at least `--min-length` characters long
2. Replaces any occurrence in the output with `[VAR_NAME]`
3. Matches longest values first to avoid partial replacements

Short values (like `1`, `true`, `/bin`) are ignored by default to avoid false positives. Adjust with `--min-length` if needed.

## Examples

```bash
# See which variable was caught
$ OPENAI_API_KEY="sk-abc123xyz" echo "Using sk-abc123xyz" \
    | OPENAI_API_KEY="sk-abc123xyz" scripts/secret-tee
Using [OPENAI_API_KEY]

# Save a debug session safely
$ source .env && out/scaffold --debug "hello" 2>&1 \
    | scripts/secret-tee debug.log

# Lower the threshold to catch shorter secrets
$ scripts/secret-tee --min-length 4
```
