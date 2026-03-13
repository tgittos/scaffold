# Evaluations

Scaffold includes an evaluation harness for measuring performance on coding benchmarks. The harness is a standalone Python project that invokes the scaffold binary via subprocess — no C code changes needed.

## Supported Benchmarks

| Benchmark | Instances | Task | Output |
|-----------|-----------|------|--------|
| [SWE-bench Verified](https://www.swebench.com/) | 500 | Fix real GitHub issues | Unified diff patch |
| [FEA-Bench](https://github.com/microsoft/FEA-Bench) | 1,401 | Implement features in real repos | Unified diff patch |
| [LiveCodeBench](https://livecodebench.github.io/) | 1,055 | Solve competitive programming problems | Python code |
| [Context-Bench](https://github.com/letta-ai/context-bench) | Varies | Answer questions by searching files | Text answer (local only) |

## Prerequisites

- A built scaffold binary (`./scripts/build.sh`)
- [uv](https://docs.astral.sh/uv/) for Python package management
- An LLM API key (`OPENAI_API_KEY` or `ANTHROPIC_API_KEY`), or an existing OAuth login (`out/scaffold --login`)

**For remote eval runs** (generates predictions AND scores them):

- A DigitalOcean account with API token (`DIGITALOCEAN_ACCESS_TOKEN`)
- `doctl` CLI installed and authenticated
- Docker-based scoring runs on the provisioned droplet — no local Docker needed

**For local prediction generation only** (no scoring):

- Run the CLI entry points directly (see [Direct CLI Usage](#direct-cli-usage))

## Quick Start — Remote Eval Runner

The `scripts/run_eval.py` script provisions a DigitalOcean droplet, runs the full eval loop (generate predictions + score them via SWE-bench Docker harness), retrieves results, and tears everything down.

```bash
# Build scaffold
./scripts/build.sh

# Source credentials (needs DIGITALOCEAN_ACCESS_TOKEN + LLM API key)
source .env

# Single SWE-bench instance (dev profile — small droplet)
./scripts/run_eval.py swebench --profile dev -i django__django-10097

# Full FEA-bench run (prod profile — 16 vCPUs, 500 GB storage)
./scripts/run_eval.py feabench --profile prod -m gpt-4o

# LiveCodeBench — generate code solutions (no Docker scoring)
./scripts/run_eval.py livecodebench --profile dev -n 10

# Keep droplet alive after run (for debugging)
./scripts/run_eval.py swebench --profile dev -i django__django-10097 --keep
```

Results are saved to `eval_results/` (or the directory specified by `-o`):

```
eval_results/
  predictions.jsonl           — merged across all runs
  sessions/
    session_{run_id}.log      — full SSH session output
  instances/
    {instance_id}/
      scaffold/               — per-instance scaffold artifacts
        stdout.log            — scaffold raw stdout (JSONL + debug)
        stderr.log            — scaffold stderr (if available)
        patch.diff            — extracted git diff (standalone)
        conversation.json     — structured scaffold conversation
        container_diagnostics.log — git status, env info, scaffold home listing
        scaffold_home/        — scaffold DB + session logs from container
      eval/                   — SWE-bench evaluation output
        report.json           — test results
  _staging/                   — ephemeral merge workspace
```

### Droplet Profiles

| Profile | Droplet | Block Storage | Cost | Use case |
|---------|---------|---------------|------|----------|
| `dev` | `s-2vcpu-4gb` | 50 GB | ~$0.036/hr | Test 1-5 instances, validate setup |
| `prod` | `c-16-intel` | 500 GB | ~$0.38/hr | Full benchmark suites |

Block storage is mounted at `/mnt/evaldata` and holds repo clones, dataset cache, Docker images, and eval results.

### CLI Reference

```
Usage: scripts/run_eval.py [OPTIONS] <benchmark>

Benchmarks: swebench, feabench, livecodebench

Options:
  -m, --model MODEL          Model to evaluate (default: claude-sonnet-4-20250514)
  -o, --output DIR           Local output directory (default: eval_results/)
  -i, --instance-ids IDS     Comma-separated instance IDs
  -n, --max-instances N      Max instances to run
  -t, --timeout SECONDS      Per-instance timeout
  --profile dev|prod         Droplet profile (default: dev)
  --region REGION            DO region (default: sfo3)
  --keep                     Don't tear down droplet after run
  --scaffold-home DIR        Scaffold home to sync OAuth from (default: ~/.local/scaffold)
  --next N                   Pick next N untested instances for the model
  --retry-failed N           Pick N previously failed instances for the model
  --filter PREFIX            Filter instances by ID prefix (with --next/--retry-failed)
  --render                   Regenerate BENCHMARKS.md from results (no eval run)
```

### Environment Variables

| Variable | Purpose |
|----------|---------|
| `DIGITALOCEAN_ACCESS_TOKEN` | DO API access (required) |
| `OPENAI_API_KEY` | Forwarded to droplet for LLM calls |
| `ANTHROPIC_API_KEY` | Forwarded to droplet for LLM calls |
| `GITHUB_TOKEN` | Forwarded to droplet for repo access |
| `CODEX_API_KEY` | Codex OAuth token (auto-exported by eval runner) |
| `CODEX_ACCOUNT_ID` | Codex account ID (auto-exported by eval runner) |

### OAuth (Codex)

If you've logged in via `out/scaffold --login`, the eval runner automatically exports your Codex token locally using `--export-codex-token` and forwards it to the droplet as `CODEX_API_KEY` / `CODEX_ACCOUNT_ID` environment variables. This lets you run evals against OpenAI Codex models without an API key.

The token is decrypted locally (where the encryption key matches) and sent as a plaintext env var to the droplet, avoiding the host-bound encryption problem with `oauth2.db`. Use `--scaffold-home` to point at a different scaffold home directory for the local token export.

### Lifecycle

1. Creates an ephemeral SSH key pair and registers it with DO
2. Provisions a droplet + block storage volume
3. Installs Docker, uv, mounts volume, copies scaffold binary + evals package
4. Generates predictions via `scaffold-eval-<benchmark>`
5. Scores predictions via the SWE-bench Docker evaluation harness
6. SCPs results back to local machine
7. Tears down droplet, volume, and SSH key

Ctrl+C or any error triggers automatic teardown via `atexit` and signal handlers. Use `--keep` to preserve the droplet for debugging.

## Installation (Manual)

If you prefer to run benchmarks locally without DO provisioning:

```bash
cd evals

# Install with SWE-bench dependencies
uv sync --extra swebench

# Or FEA-bench
uv sync --extra feabench

# Or LiveCodeBench
uv sync --extra livecodebench

# Or Context-bench
uv sync --extra contextbench
```

## Direct CLI Usage

Each benchmark has its own CLI entry point installed by `uv sync`:

### SWE-bench

```bash
cd evals && source ../.env

# Run specific instances
uv run scaffold-eval-swebench \
    --scaffold-binary ../out/scaffold \
    --model claude-sonnet-4-20250514 \
    --output predictions.jsonl \
    --instance-ids django__django-10097 sympy__sympy-20442

# Run first 10 instances
uv run scaffold-eval-swebench \
    --scaffold-binary ../out/scaffold \
    --model claude-sonnet-4-20250514 \
    --output predictions.jsonl \
    --max-instances 10
```

### FEA-Bench

FEA-bench repos aren't in swebench's repo maps. Before running locally, patch the installed swebench package:

```bash
uv run python -m scaffold_evals.feabench.patch_swebench
```

This is done automatically by `run_eval.py` for remote runs. Then generate predictions:

```bash
uv run scaffold-eval-feabench \
    --scaffold-binary ../out/scaffold \
    --model claude-sonnet-4-20250514 \
    --output predictions.jsonl \
    --timeout 900
```

### LiveCodeBench

LiveCodeBench uses HuggingFace streaming to avoid downloading the full dataset to disk.

```bash
cd evals && source ../.env

# Run specific problems
uv run scaffold-eval-livecodebench \
    --scaffold-binary ../out/scaffold \
    --model claude-sonnet-4-20250514 \
    --output predictions.jsonl \
    --instance-ids <question-id-1> <question-id-2>

# Run first 10 problems
uv run scaffold-eval-livecodebench \
    --scaffold-binary ../out/scaffold \
    --model claude-sonnet-4-20250514 \
    --output predictions.jsonl \
    --max-instances 10

# Use a specific dataset version (default: release_v6)
uv run scaffold-eval-livecodebench \
    --scaffold-binary ../out/scaffold \
    --model claude-sonnet-4-20250514 \
    --output predictions.jsonl \
    --version release_v5 \
    --max-instances 10

```

Predictions are saved in LiveCodeBench's expected format (`code_list` field) and can be scored using [LiveCodeBench's evaluation tool](https://github.com/LiveCodeBench/LiveCodeBench):

```bash
git clone https://github.com/LiveCodeBench/LiveCodeBench.git
cd LiveCodeBench && uv pip install -e .
python -m lcb_runner.evaluation.compute_code_generation_metrics \
    --eval_all_file predictions.jsonl \
    --start_date 2023-05-01 --end_date 2025-01-31
```

### Context-Bench

```bash
uv run scaffold-eval-contextbench \
    --scaffold-binary ../out/scaffold \
    --model claude-sonnet-4-20250514 \
    --data-dir /path/to/file/collection \
    --questions /path/to/questions.jsonl \
    --output results.jsonl
```

Context-bench questions file is JSONL with one object per line:

```json
{"id": "q1", "question": "What API version does the auth module use?", "expected": "v2.3"}
```

## Evaluating Results

### SWE-bench / FEA-bench

The runners produce a `predictions.jsonl` file compatible with the [SWE-bench evaluation harness](https://github.com/princeton-nlp/SWE-bench). Each line contains:

```json
{"instance_id": "django__django-10097", "model_name_or_path": "claude-sonnet-4-20250514", "model_patch": "diff --git a/..."}
```

To evaluate predictions manually (requires Docker and significant disk space):

```bash
uv run python -m scaffold_evals.swebench.evaluate \
    --predictions predictions.jsonl \
    --run-id scaffold_v1
```

This wraps `swebench.harness.run_evaluation`, which builds Docker containers for each instance, applies the patch, and runs the test suite.

### Context-Bench

Results are written as JSONL:

```json
{"id": "q1", "question": "...", "expected": "v2.3", "answer": "v2.3", "exit_code": 0}
```

Compare `answer` against `expected` using your preferred metric (exact match, fuzzy match, LLM-as-judge).

## How It Works

For each benchmark instance, the harness:

1. **Prepares the environment** — Clones the target repo (SWE-bench/FEA-bench) or points at a data directory (Context-bench) and checks out the correct commit.

2. **Writes a system prompt** — Fills a prompt template with the issue text or question, writes it to a temp file.

3. **Invokes scaffold** — Runs the binary in single-shot mode:
   ```
   out/scaffold --json --yolo --system-prompt-file /tmp/prompt.txt \
       --home /tmp/eval/home_<instance> --model <model> "<message>"
   ```
   - `--json` produces structured JSONL on stdout for trace capture
   - `--yolo` disables all approval gates (unattended execution)
   - `--system-prompt-file` provides the per-instance prompt (scaffold reads and deletes it)
   - `--home` isolates scaffold's state per instance
   - `cwd` is set to the target repo so scaffold's tools operate on it

4. **Extracts results** — For patch benchmarks, runs `git add -A && git diff --cached`. For Context-bench, extracts the last assistant text message from the JSONL output.

5. **Appends to output** — Writes each prediction/result to the output JSONL file immediately (crash-resilient).

## Configuration

The runners accept a `--config` flag pointing to a TOML file:

```toml
scaffold_binary = "../out/scaffold"
model = "gpt-4o"
timeout = 900
workdir = "/tmp/eval"
```

Environment variables (`OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, `GITHUB_TOKEN`) always take precedence over config file values.

## CI Workflow

The eval harness includes a GitHub Actions workflow (`.github/workflows/eval.yml`) for running SWE-bench and FEA-bench evaluations. It is triggered manually via `workflow_dispatch`:

1. Go to **Actions > Eval** in the GitHub UI
2. Select the benchmark (swebench or feabench)
3. Choose the model
4. Optionally provide instance IDs or a max instance count
5. Run the workflow

The workflow builds scaffold in the devcontainer, installs eval dependencies, runs the benchmark, and uploads `predictions.jsonl` as an artifact.

## Project Structure

```
evals/
  pyproject.toml                     # Python package (uv-managed)
  prompts/
    swebench_system.txt              # Bug fix system prompt template
    feabench_system.txt              # Feature implementation prompt template
    contextbench_system.txt          # Information retrieval prompt template
  scaffold_evals/
    common/
      config.py                      # TOML config + env var overrides
      scaffold_runner.py             # Invoke scaffold binary, parse JSONL
      patch_extractor.py             # git diff extraction
      instance_loader.py             # HuggingFace dataset loading + repo setup
      benchmark_runner.py            # Shared runner logic for patch benchmarks
    swebench/
      runner.py                      # SWE-bench CLI entry point
      prompt.py                      # Prompt builder
      evaluate.py                    # swebench.harness wrapper
    feabench/
      runner.py                      # FEA-bench CLI entry point
      prompt.py                      # Prompt builder
      patch_swebench.py              # Patches swebench 4.1.0 for FEA-bench compatibility
    livecodebench/
      runner.py                      # LiveCodeBench CLI entry point (streaming dataset)
    contextbench/
      runner.py                      # Context-bench CLI entry point
      prompt.py                      # Prompt builder
```

## Benchmark Tracking

Results are tracked in data files under `benchmarks/`:

```
benchmarks/
  instances/
    swebench.txt          # Valid instance IDs (one per line, guard rail)
  results.json            # Accumulated pass/fail results per model
```

After each eval run, `run_eval.py` automatically:
1. Parses the SWE-bench report for resolved/unresolved instance IDs
2. Merges results into `benchmarks/results.json` (additive, never overwrites)
3. Regenerates `BENCHMARKS.md` as a scorecard

### Instance selection

Instead of manually specifying instance IDs, use `--next` or `--retry-failed`:

```bash
# Run next 5 untested instances for the current model
./scripts/run_eval.py swebench --profile dev --next 5

# Run next 5 untested django instances only
./scripts/run_eval.py swebench --profile dev --next 5 --filter django

# Retry failed astropy instances
./scripts/run_eval.py swebench --profile dev --retry-failed 3 --filter astropy

# Just regenerate the scorecard (no eval run)
./scripts/run_eval.py swebench --render
```

`--next` and `--retry-failed` are mutually exclusive with `-i`. `--filter` takes a prefix string and restricts which instances are considered (e.g., `django`, `astropy__astropy`, `sympy`).
