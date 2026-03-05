#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# ///
"""Provision a DigitalOcean droplet, run scaffold evals, score them, retrieve results.

Supports swebench and feabench benchmarks. Handles the full lifecycle:
  1. Create ephemeral SSH key + DO droplet + block storage volume
  2. Install Docker, uv, mount volume, copy scaffold binary + evals package
  3. Generate predictions via scaffold-eval-<benchmark>
  4. Score predictions via SWE-bench Docker evaluation harness
  5. SCP scored results back to local machine
  6. Tear down all DO resources

Usage:
  source .env && ./scripts/run_eval.py swebench --profile dev -i django__django-16379
"""

from __future__ import annotations

import argparse
import atexit
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from datetime import date
from pathlib import Path

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

PROFILES = {
    "dev": {"droplet_size": "s-2vcpu-4gb", "volume_gb": 50, "ncpus": 2},
    "prod": {"droplet_size": "c-16-intel", "volume_gb": 500, "ncpus": 16},
}

SUPPORTED_BENCHMARKS = ("swebench", "feabench")
DEFAULT_MODEL = "claude-sonnet-4-20250514"
DEFAULT_REGION = "sfo3"
MOUNT_POINT = "/mnt/evaldata"
SSH_CONNECT_TIMEOUT = 120  # seconds
SSH_POLL_INTERVAL = 5  # seconds
DROPLET_IMAGE = "ubuntu-24-04-x64"

# Env vars forwarded to the remote droplet
FORWARDED_ENV_VARS = (
    "OPENAI_API_KEY",
    "ANTHROPIC_API_KEY",
    "GITHUB_TOKEN",
)

# ---------------------------------------------------------------------------
# Globals for teardown
# ---------------------------------------------------------------------------

_cleanup_state: dict = {}


# ---------------------------------------------------------------------------
# Helpers — DO API via doctl
# ---------------------------------------------------------------------------


def doctl(*args: str, capture: bool = True) -> str:
    """Run a doctl command, returning stdout."""
    cmd = ["doctl", *args, "--output", "json"]
    result = subprocess.run(cmd, capture_output=capture, text=True, check=True)
    return result.stdout.strip() if capture else ""


def doctl_no_json(*args: str, capture: bool = True) -> str:
    """Run a doctl command without --output json."""
    cmd = ["doctl", *args]
    result = subprocess.run(cmd, capture_output=capture, text=True, check=True)
    return result.stdout.strip() if capture else ""


# ---------------------------------------------------------------------------
# SSH key management
# ---------------------------------------------------------------------------


def create_ssh_key(tmp_dir: str) -> tuple[str, str, str]:
    """Generate an ephemeral ed25519 key pair and register it with DO.

    Returns (key_path, pub_key_path, do_key_id).
    """
    key_path = os.path.join(tmp_dir, "eval_key")
    pub_path = key_path + ".pub"

    subprocess.run(
        ["ssh-keygen", "-t", "ed25519", "-f", key_path, "-N", "", "-q"],
        check=True,
    )

    with open(pub_path) as f:
        pub_key = f.read().strip()

    key_name = f"scaffold-eval-{int(time.time())}"
    out = doctl("compute", "ssh-key", "import", key_name, "--public-key-file", pub_path)
    key_data = json.loads(out)
    key_id = str(key_data[0]["id"])

    print(f"[ssh] Created ephemeral key {key_name} (id={key_id})")
    return key_path, pub_path, key_id


# ---------------------------------------------------------------------------
# Volume management
# ---------------------------------------------------------------------------


def create_volume(name: str, size_gb: int, region: str) -> str:
    """Create a block storage volume, return its ID."""
    out = doctl(
        "compute", "volume", "create", name,
        "--region", region,
        "--size", f"{size_gb}GiB",
        "--fs-type", "ext4",
    )
    vol = json.loads(out)
    vol_id = vol[0]["id"]
    print(f"[volume] Created {name} ({size_gb} GB) id={vol_id}")
    return vol_id


def attach_volume(volume_id: str, droplet_id: str) -> None:
    """Attach a volume to a droplet, retrying if the droplet isn't ready yet."""
    for attempt in range(6):
        try:
            doctl_no_json(
                "compute", "volume-action", "attach", volume_id,
                droplet_id,
                "--wait",
            )
            print(f"[volume] Attached {volume_id} to droplet {droplet_id}")
            return
        except subprocess.CalledProcessError:
            if attempt == 5:
                raise
            print(f"[volume] Attach attempt {attempt + 1} failed, retrying in 10s...")
            time.sleep(10)


# ---------------------------------------------------------------------------
# Droplet management
# ---------------------------------------------------------------------------


def create_droplet(
    name: str,
    profile: str,
    region: str,
    ssh_key_id: str,
) -> tuple[str, str]:
    """Create a droplet, return (droplet_id, ip_address)."""
    size = PROFILES[profile]["droplet_size"]

    out = doctl(
        "compute", "droplet", "create", name,
        "--region", region,
        "--size", size,
        "--image", DROPLET_IMAGE,
        "--ssh-keys", ssh_key_id,
        "--wait",
    )
    data = json.loads(out)
    droplet = data[0]
    droplet_id = str(droplet["id"])

    # Extract public IPv4
    ip = ""
    for net in droplet.get("networks", {}).get("v4", []):
        if net.get("type") == "public":
            ip = net["ip_address"]
            break

    if not ip:
        # Fallback: query again
        time.sleep(2)
        out2 = doctl("compute", "droplet", "get", droplet_id)
        d2 = json.loads(out2)
        for net in d2[0].get("networks", {}).get("v4", []):
            if net.get("type") == "public":
                ip = net["ip_address"]
                break

    if not ip:
        raise RuntimeError(f"Could not determine IP for droplet {droplet_id}")

    print(f"[droplet] Created {name} ({size}) id={droplet_id} ip={ip}")
    return droplet_id, ip


# ---------------------------------------------------------------------------
# SSH operations
# ---------------------------------------------------------------------------


def _ssh_opts(key_path: str) -> list[str]:
    return [
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-o", "LogLevel=ERROR",
        "-o", "ConnectTimeout=10",
        "-i", key_path,
    ]


def wait_for_ssh(ip: str, key_path: str) -> None:
    """Poll until SSH is reachable."""
    deadline = time.time() + SSH_CONNECT_TIMEOUT
    print(f"[ssh] Waiting for SSH on {ip}...", end="", flush=True)
    while time.time() < deadline:
        result = subprocess.run(
            ["ssh", *_ssh_opts(key_path), f"root@{ip}", "true"],
            capture_output=True,
            timeout=15,
        )
        if result.returncode == 0:
            print(" ready")
            return
        print(".", end="", flush=True)
        time.sleep(SSH_POLL_INTERVAL)
    print()
    raise TimeoutError(f"SSH not reachable on {ip} after {SSH_CONNECT_TIMEOUT}s")


def ssh_run(
    ip: str,
    key_path: str,
    cmd: str,
    check: bool = True,
    log_path: str | None = None,
) -> int:
    """Run a command over SSH, streaming output to local terminal.

    Environment variables are sourced from /root/.eval_env on the droplet
    (written by upload_env_file), keeping secrets out of process args.

    If log_path is provided, output is tee'd to the file while still streaming
    to the terminal.
    """
    prefixed = f"[ -f /root/.eval_env ] && . /root/.eval_env; {cmd}"
    full_cmd = ["ssh", *_ssh_opts(key_path), f"root@{ip}", prefixed]

    if log_path:
        parent = os.path.dirname(log_path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(log_path, "w", buffering=1) as log_file:
            proc = subprocess.Popen(
                full_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )
            try:
                for line in iter(proc.stdout.readline, b""):
                    decoded = line.decode("utf-8", errors="replace")
                    sys.stdout.write(decoded)
                    sys.stdout.flush()
                    log_file.write(decoded)
            finally:
                proc.stdout.close()
                proc.wait()
            returncode = proc.returncode
    else:
        result = subprocess.run(full_cmd)
        returncode = result.returncode

    if check and returncode != 0:
        raise subprocess.CalledProcessError(returncode, cmd)
    return returncode


def upload_env_file(ip: str, key_path: str, env: dict[str, str]) -> None:
    """Write environment variables to a file on the droplet.

    This avoids passing secrets as SSH command-line arguments (visible in ps).
    """
    lines = [f"export {k}={_shell_quote(v)}" for k, v in env.items() if v]
    content = "\n".join(lines) + "\n"
    # Pipe content via stdin to avoid it appearing in process args
    subprocess.run(
        ["ssh", *_ssh_opts(key_path), f"root@{ip}",
         "cat > /root/.eval_env && chmod 600 /root/.eval_env"],
        input=content.encode(),
        check=True,
    )


def scp_to(ip: str, key_path: str, local: str, remote: str) -> None:
    """Upload file or directory to droplet."""
    cmd = ["scp", *_ssh_opts(key_path), "-r", local, f"root@{ip}:{remote}"]
    subprocess.run(cmd, check=True)


def scp_from(ip: str, key_path: str, remote: str, local: str) -> None:
    """Download file or directory from droplet."""
    cmd = ["scp", *_ssh_opts(key_path), "-r", f"root@{ip}:{remote}", local]
    subprocess.run(cmd, check=True)


def _shell_quote(s: str) -> str:
    """Simple shell quoting."""
    return "'" + s.replace("'", "'\"'\"'") + "'"


# ---------------------------------------------------------------------------
# Remote setup
# ---------------------------------------------------------------------------


def setup_droplet(
    ip: str,
    key_path: str,
    volume_name: str,
    scaffold_binary: str,
    evals_dir: str,
    benchmark: str,
) -> None:
    """Install dependencies, mount volume, copy artifacts to the droplet."""
    print("[setup] Installing packages on droplet...")
    ssh_run(ip, key_path, (
        "export DEBIAN_FRONTEND=noninteractive && "
        "cloud-init status --wait > /dev/null 2>&1 || true && "
        "apt-get update -qq && "
        "apt-get install -y -qq git python3 python3-venv curl docker.io"
    ))

    print("[setup] Installing uv...")
    ssh_run(ip, key_path, "curl -LsSf https://astral.sh/uv/install.sh | sh")
    # Ensure uv is on PATH for subsequent commands
    ssh_run(ip, key_path, "echo 'export PATH=\"$HOME/.local/bin:$PATH\"' >> ~/.bashrc")

    print(f"[setup] Mounting volume {volume_name} at {MOUNT_POINT}...")
    # DO volumes attach as /dev/disk/by-id/scsi-0DO_Volume_<name>
    vol_dev = f"/dev/disk/by-id/scsi-0DO_Volume_{volume_name}"
    ssh_run(ip, key_path, (
        f"mkdir -p {MOUNT_POINT} && "
        # Only format if not already formatted
        f"blkid {vol_dev} || mkfs.ext4 -F {vol_dev} && "
        f"mount -o defaults,noatime {vol_dev} {MOUNT_POINT} && "
        f"mkdir -p {MOUNT_POINT}/docker {MOUNT_POINT}/work {MOUNT_POINT}/results"
    ))

    print("[setup] Configuring Docker with volume-backed storage...")
    ssh_run(ip, key_path, (
        f'mkdir -p /etc/docker && '
        f'echo \'{{"data-root": "{MOUNT_POINT}/docker"}}\' > /etc/docker/daemon.json && '
        f'systemctl restart docker'
    ))

    print("[setup] Copying scaffold binary...")
    ssh_run(ip, key_path, "mkdir -p /usr/local/bin")
    scp_to(ip, key_path, scaffold_binary, "/usr/local/bin/scaffold")
    ssh_run(ip, key_path, "chmod +x /usr/local/bin/scaffold")

    print("[setup] Copying evals package...")
    # Use tar to exclude .venv and __pycache__ (scp -r would copy them)
    subprocess.run(
        f"tar -C {_shell_quote(os.path.dirname(evals_dir))} "
        f"--exclude=.venv --exclude=__pycache__ --exclude='*.pyc' "
        f"-cf - {_shell_quote(os.path.basename(evals_dir))} | "
        f"ssh {' '.join(_ssh_opts(key_path))} root@{ip} 'tar -C /root -xf -'",
        shell=True, check=True,
    )

    print(f"[setup] Installing eval dependencies (extra={benchmark})...")
    ssh_run(ip, key_path, (
        f"export PATH=\"$HOME/.local/bin:$PATH\" && "
        f"cd /root/evals && uv sync --extra {benchmark}"
    ))

    print("[setup] Droplet ready")


# ---------------------------------------------------------------------------
# Benchmark execution
# ---------------------------------------------------------------------------


def run_benchmark(
    ip: str,
    key_path: str,
    benchmark: str,
    model: str,
    instance_ids: list[str] | None,
    max_instances: int | None,
    timeout: int | None,
    local_output_dir: str,
) -> None:
    """Generate predictions on the remote droplet."""
    print(f"\n[benchmark] Running {benchmark} (model={model})...")

    cmd_parts = [
        "export PATH=\"$HOME/.local/bin:$PATH\" &&",
        "cd /root/evals &&",
        f"uv run scaffold-eval-{benchmark}",
        "--scaffold-binary /usr/local/bin/scaffold",
        f"--model {model}",
        f"--workdir {MOUNT_POINT}/work",
        f"--output {MOUNT_POINT}/predictions.jsonl",
        "--debug",
    ]

    if instance_ids:
        cmd_parts.append("--instance-ids " + " ".join(instance_ids))
    if max_instances is not None:
        cmd_parts.append(f"--max-instances {max_instances}")
    if timeout is not None:
        cmd_parts.append(f"--timeout {timeout}")

    session_log = os.path.join(local_output_dir, "session.log")
    ssh_run(ip, key_path, " ".join(cmd_parts), log_path=session_log)
    print(f"[benchmark] Predictions complete (session log: {session_log})")


EVAL_DATASETS = {
    "swebench": "princeton-nlp/SWE-bench_Verified",
    "feabench": "microsoft/FEA-Bench",
}


def run_evaluation(
    ip: str,
    key_path: str,
    benchmark: str,
    run_id: str,
    ncpus: int,
) -> None:
    """Score predictions using the SWE-bench Docker evaluation harness."""
    dataset = EVAL_DATASETS.get(benchmark)
    if not dataset:
        raise ValueError(f"No evaluation dataset configured for benchmark: {benchmark}")

    print(f"\n[eval] Scoring predictions (run_id={run_id}, workers={ncpus})...")

    cmd = (
        f"export PATH=\"$HOME/.local/bin:$PATH\" && "
        f"cd /root/evals && "
        f"uv run python -m scaffold_evals.swebench.evaluate "
        f"--predictions {MOUNT_POINT}/predictions.jsonl "
        f"--run-id {run_id} "
        f"--max-workers {ncpus} "
        f"--dataset {dataset}"
    )

    ssh_run(ip, key_path, cmd)
    print("[eval] Scoring complete")


def retrieve_results(
    ip: str,
    key_path: str,
    local_output_dir: str,
) -> None:
    """Download predictions and evaluation results from the droplet."""
    import shutil

    # Remove stale results from previous runs so old files don't linger
    for subdir in ("scaffold_logs", "results", "eval_logs"):
        stale = os.path.join(local_output_dir, subdir)
        if os.path.isdir(stale):
            shutil.rmtree(stale)

    os.makedirs(local_output_dir, exist_ok=True)

    print(f"\n[results] Retrieving results to {local_output_dir}/...")

    # Predictions JSONL
    scp_from(
        ip, key_path,
        f"{MOUNT_POINT}/predictions.jsonl",
        os.path.join(local_output_dir, "predictions.jsonl"),
    )

    # Evaluation results directory (if it exists)
    rc = ssh_run(
        ip, key_path,
        f"test -d {MOUNT_POINT}/results && ls {MOUNT_POINT}/results/",
        check=False,
    )
    if rc == 0:
        scp_from(
            ip, key_path,
            f"{MOUNT_POINT}/results/",
            os.path.join(local_output_dir, "results"),
        )

    # Scaffold per-instance logs (stdout/stderr)
    rc = ssh_run(
        ip, key_path,
        f"test -d {MOUNT_POINT}/work/logs && ls {MOUNT_POINT}/work/logs/",
        check=False,
    )
    if rc == 0:
        scp_from(
            ip, key_path,
            f"{MOUNT_POINT}/work/logs/",
            os.path.join(local_output_dir, "scaffold_logs"),
        )

    # Also grab any swebench evaluation output dirs
    rc = ssh_run(
        ip, key_path,
        f"ls /root/evals/logs/ 2>/dev/null",
        check=False,
    )
    if rc == 0:
        scp_from(
            ip, key_path,
            "/root/evals/logs/",
            os.path.join(local_output_dir, "eval_logs"),
        )

    print(f"[results] Saved to {local_output_dir}/")


# ---------------------------------------------------------------------------
# Teardown
# ---------------------------------------------------------------------------


def teardown(
    droplet_id: str | None,
    volume_id: str | None,
    ssh_key_id: str | None,
) -> None:
    """Destroy all DO resources."""
    if droplet_id:
        print(f"[teardown] Destroying droplet {droplet_id}...")
        try:
            # Detach volume before destroying droplet
            if volume_id:
                try:
                    doctl_no_json(
                        "compute", "volume-action", "detach", volume_id,
                        droplet_id,
                        "--wait",
                    )
                except subprocess.CalledProcessError:
                    pass  # May already be detached

            doctl_no_json("compute", "droplet", "delete", droplet_id, "--force")
        except subprocess.CalledProcessError as e:
            print(f"[teardown] Warning: droplet delete failed: {e}", file=sys.stderr)

    if volume_id:
        print(f"[teardown] Destroying volume {volume_id}...")
        # Volume deletion may need a brief wait after droplet destruction
        time.sleep(5)
        try:
            doctl_no_json("compute", "volume", "delete", volume_id, "--force")
        except subprocess.CalledProcessError as e:
            print(f"[teardown] Warning: volume delete failed: {e}", file=sys.stderr)

    if ssh_key_id:
        print(f"[teardown] Removing SSH key {ssh_key_id}...")
        try:
            doctl_no_json("compute", "ssh-key", "delete", ssh_key_id, "--force")
        except subprocess.CalledProcessError as e:
            print(f"[teardown] Warning: SSH key delete failed: {e}", file=sys.stderr)

    print("[teardown] Done")


def _atexit_teardown() -> None:
    """Teardown handler registered with atexit."""
    state = _cleanup_state
    if state.get("keep") or state.get("torn_down"):
        return
    state["torn_down"] = True
    print("\n[cleanup] Running teardown...", file=sys.stderr)
    teardown(
        state.get("droplet_id"),
        state.get("volume_id"),
        state.get("ssh_key_id"),
    )


def _signal_handler(signum: int, frame: object) -> None:
    """Handle SIGINT/SIGTERM by triggering teardown."""
    name = signal.Signals(signum).name
    print(f"\n[signal] Caught {name}, tearing down...", file=sys.stderr)
    _atexit_teardown()
    sys.exit(128 + signum)


# ---------------------------------------------------------------------------
# Benchmark tracking
# ---------------------------------------------------------------------------

# Paths relative to repo root
BENCHMARKS_DIR = Path(__file__).resolve().parent.parent / "benchmarks"
INSTANCES_DIR = BENCHMARKS_DIR / "instances"
RESULTS_PATH = BENCHMARKS_DIR / "results.json"
SCORECARD_PATH = Path(__file__).resolve().parent.parent / "BENCHMARKS.md"


def load_results() -> dict:
    """Load benchmarks/results.json, returning empty dict if missing."""
    if RESULTS_PATH.exists():
        return json.loads(RESULTS_PATH.read_text())
    return {}


def save_results(results: dict) -> None:
    """Write benchmarks/results.json with sorted keys."""
    RESULTS_PATH.parent.mkdir(parents=True, exist_ok=True)
    RESULTS_PATH.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")


def load_registry(benchmark: str) -> list[str]:
    """Load the instance registry for a benchmark (one ID per line)."""
    path = INSTANCES_DIR / f"{benchmark}.txt"
    if not path.exists():
        print(f"Error: No instance registry at {path}", file=sys.stderr)
        sys.exit(1)
    return [line.strip() for line in path.read_text().splitlines() if line.strip()]


def select_instances(
    benchmark: str, model: str, mode: str, count: int,
) -> list[str]:
    """Select instances based on mode ('next' or 'retry-failed').

    Returns up to `count` instance IDs from the registry that match the mode.
    """
    registry = load_registry(benchmark)
    results = load_results().get(benchmark, {})

    selected = []
    for instance_id in registry:
        if len(selected) >= count:
            break
        instance_results = results.get(instance_id, {})
        model_result = instance_results.get(model)

        if mode == "next" and model_result is None:
            selected.append(instance_id)
        elif mode == "retry-failed" and model_result and model_result.get("result") == "fail":
            selected.append(instance_id)

    return selected


def update_benchmark_results(
    output_dir: str, benchmark: str, model: str, run_id: str,
) -> None:
    """Parse eval report and merge results into benchmarks/results.json."""
    resolved = set()
    unresolved = set()

    # Strategy 1: Look for a summary report in eval_results/results/
    results_dir = Path(output_dir) / "results"
    if results_dir.exists():
        report_file = None
        for pattern in [f"{run_id}.json", f"*.{run_id}.json"]:
            matches = list(results_dir.glob(pattern))
            if matches:
                report_file = matches[0]
                break
        if not report_file:
            for f in results_dir.rglob("*.json"):
                if run_id in f.name:
                    report_file = f
                    break
        if report_file:
            print(f"[benchmark] Reading report: {report_file}")
            report = json.loads(report_file.read_text())
            resolved = set(report.get("resolved_ids", report.get("resolved", [])))
            unresolved = set(report.get("unresolved_ids", report.get("unresolved", [])))

    # Strategy 2: Aggregate per-instance report.json files from eval_logs
    if not resolved and not unresolved:
        eval_logs = Path(output_dir) / "eval_logs" / "run_evaluation"
        if eval_logs.exists():
            for report_file in eval_logs.rglob("report.json"):
                report = json.loads(report_file.read_text())
                for iid, info in report.items():
                    if info.get("resolved"):
                        resolved.add(iid)
                    else:
                        unresolved.add(iid)

    if not resolved and not unresolved:
        print(f"[benchmark] No report file found matching run_id={run_id}")
        return

    # Merge into results.json
    all_results = load_results()
    bench_results = all_results.setdefault(benchmark, {})
    today = date.today().isoformat()

    for iid in resolved:
        bench_results.setdefault(iid, {})[model] = {"result": "pass", "date": today}
    for iid in unresolved:
        bench_results.setdefault(iid, {})[model] = {"result": "fail", "date": today}

    save_results(all_results)
    new_count = len(resolved) + len(unresolved)
    print(f"[benchmark] Updated results.json: {len(resolved)} pass, {len(unresolved)} fail ({new_count} new)")

    # Regenerate scorecard
    render_scorecard(all_results)


def render_scorecard(results: dict | None = None) -> None:
    """Generate BENCHMARKS.md from results.json + instance registries."""
    if results is None:
        results = load_results()
    lines = [
        "# Benchmark Scorecard",
        "",
        "*Generated by `scripts/run_eval.py --render`. Do not edit manually.*",
        "",
    ]

    for benchmark in sorted(results.keys()):
        bench_results = results[benchmark]
        registry = load_registry(benchmark) if (INSTANCES_DIR / f"{benchmark}.txt").exists() else []
        total = len(registry)

        # Discover all models
        models = sorted({
            model
            for instance_data in bench_results.values()
            for model in instance_data
        })

        if not models:
            continue

        benchmark_label = {
            "swebench": "SWE-bench Verified",
            "feabench": "FEA-Bench",
        }.get(benchmark, benchmark)

        lines.append(f"## {benchmark_label} ({total} instances)")
        lines.append("")

        # Summary table
        lines.append("### Summary")
        lines.append("| Model | Passed | Failed | Pending | Pass Rate |")
        lines.append("|-------|--------|--------|---------|-----------|")

        for model in models:
            passed = sum(
                1 for idata in bench_results.values()
                if idata.get(model, {}).get("result") == "pass"
            )
            failed = sum(
                1 for idata in bench_results.values()
                if idata.get(model, {}).get("result") == "fail"
            )
            pending = total - passed - failed
            rate = f"{100 * passed / (passed + failed):.1f}%" if (passed + failed) > 0 else "N/A"
            lines.append(f"| {model} | {passed} | {failed} | {pending} | {rate} |")

        lines.append("")

        # Results table (only rows with results)
        tested_ids = sorted(
            iid for iid in bench_results if bench_results[iid]
        )

        if tested_ids:
            lines.append("### Results")
            header = "| Instance | " + " | ".join(models) + " |"
            sep = "|----------|" + "|".join("-" * (len(m) + 2) for m in models) + "|"
            lines.append(header)
            lines.append(sep)

            for iid in tested_ids:
                cells = []
                for model in models:
                    r = bench_results[iid].get(model)
                    if r is None:
                        cells.append("")
                    elif r["result"] == "pass":
                        cells.append(f":white_check_mark: {r['date']}")
                    else:
                        cells.append(f":x: {r['date']}")
                lines.append(f"| {iid} | " + " | ".join(cells) + " |")

            lines.append("")

        # Instance registry (collapsible)
        if registry:
            lines.append("### Instance Registry")
            lines.append(f"<details><summary>All {total} valid instance IDs</summary>")
            lines.append("")
            for iid in registry:
                lines.append(f"- {iid}")
            lines.append("")
            lines.append("</details>")
            lines.append("")

    SCORECARD_PATH.write_text("\n".join(lines))
    print(f"[benchmark] Generated {SCORECARD_PATH} ({len(lines)} lines)")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run scaffold evals on a DigitalOcean droplet",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  ./scripts/run_eval.py swebench --profile dev -i django__django-16379\n"
            "  ./scripts/run_eval.py feabench --profile prod -m gpt-4o\n"
            "  ./scripts/run_eval.py swebench --profile prod -n 50\n"
        ),
    )
    parser.add_argument(
        "benchmark",
        choices=SUPPORTED_BENCHMARKS,
        help="Benchmark to run (swebench, feabench)",
    )
    parser.add_argument(
        "-m", "--model",
        default=DEFAULT_MODEL,
        help=f"Model to evaluate (default: {DEFAULT_MODEL})",
    )
    parser.add_argument(
        "-o", "--output",
        default="eval_results",
        help="Local output directory (default: eval_results/)",
    )
    parser.add_argument(
        "-i", "--instance-ids",
        help="Comma-separated instance IDs",
    )
    parser.add_argument(
        "-n", "--max-instances",
        type=int,
        help="Max instances to run",
    )
    parser.add_argument(
        "-t", "--timeout",
        type=int,
        help="Per-instance timeout in seconds",
    )
    parser.add_argument(
        "--profile",
        choices=PROFILES.keys(),
        default="dev",
        help="Droplet profile (default: dev)",
    )
    parser.add_argument(
        "--region",
        default=DEFAULT_REGION,
        help=f"DO region (default: {DEFAULT_REGION})",
    )
    parser.add_argument(
        "--keep",
        action="store_true",
        help="Don't tear down droplet after run (for debugging)",
    )
    parser.add_argument(
        "--scaffold-home",
        default=os.path.expanduser("~/.local/scaffold"),
        help="Scaffold home dir to sync OAuth credentials from (default: ~/.local/scaffold)",
    )
    parser.add_argument(
        "--next",
        type=int,
        metavar="N",
        help="Pick the next N untested instances for the given model",
    )
    parser.add_argument(
        "--retry-failed",
        type=int,
        metavar="N",
        help="Pick N previously failed instances for the given model",
    )
    parser.add_argument(
        "--render",
        action="store_true",
        help="Regenerate BENCHMARKS.md from current results (no eval run)",
    )
    return parser.parse_args()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> None:
    args = parse_args()

    # --render: just regenerate scorecard and exit
    if args.render:
        render_scorecard()
        return

    # Validate mutual exclusivity of instance selection flags
    selection_flags = sum(1 for f in [args.instance_ids, args.next, args.retry_failed] if f)
    if selection_flags > 1:
        print("Error: --next, --retry-failed, and -i are mutually exclusive.", file=sys.stderr)
        sys.exit(1)

    # Validate environment
    do_token = os.environ.get("DIGITALOCEAN_ACCESS_TOKEN")
    if not do_token:
        print("Error: DIGITALOCEAN_ACCESS_TOKEN not set. Run 'source .env' first.", file=sys.stderr)
        sys.exit(1)

    has_api_key = os.environ.get("OPENAI_API_KEY") or os.environ.get("ANTHROPIC_API_KEY")
    if not has_api_key:
        print("Error: No API key found (OPENAI_API_KEY or ANTHROPIC_API_KEY). Run 'source .env' first.", file=sys.stderr)
        sys.exit(1)

    # Resolve paths
    script_dir = Path(__file__).resolve().parent
    root_dir = script_dir.parent
    scaffold_binary = str(root_dir / "out" / "scaffold")
    evals_dir = str(root_dir / "evals")

    if not os.path.isfile(scaffold_binary):
        print(f"Error: scaffold binary not found at {scaffold_binary}", file=sys.stderr)
        print("Run ./scripts/build.sh first.", file=sys.stderr)
        sys.exit(1)

    if not os.path.isdir(evals_dir):
        print(f"Error: evals directory not found at {evals_dir}", file=sys.stderr)
        sys.exit(1)

    # Export Codex token locally (if logged in) and add to forwarded env
    codex_token = None
    codex_account_id = None
    try:
        export_cmd = ["bash", scaffold_binary, "--home", args.scaffold_home, "--export-codex-token"]
        result = subprocess.run(export_cmd, capture_output=True, text=True, timeout=15)
        if result.returncode == 0:
            creds = json.loads(result.stdout)
            codex_token = creds.get("token")
            codex_account_id = creds.get("account_id")
            print(f"[codex] Exported Codex token (account: {codex_account_id})")
        else:
            print(f"[codex] No Codex token available ({result.stderr.strip()})")
    except (subprocess.TimeoutExpired, json.JSONDecodeError, FileNotFoundError, OSError) as e:
        print(f"[codex] Codex token export skipped: {e}")

    # Build env dict to upload to the droplet
    remote_env = {var: os.environ[var] for var in FORWARDED_ENV_VARS if os.environ.get(var)}
    if codex_token:
        remote_env["CODEX_API_KEY"] = codex_token
    if codex_account_id:
        remote_env["CODEX_ACCOUNT_ID"] = codex_account_id

    profile = PROFILES[args.profile]
    ts = int(time.time())
    resource_name = f"scaffold-eval-{ts}"
    run_id = f"scaffold_{ts}"

    effective_model = "gpt-5.3-codex" if codex_token else args.model

    # Resolve instance selection
    if args.instance_ids:
        instance_ids = args.instance_ids.split(",")
    elif args.next or args.retry_failed:
        mode = "next" if args.next else "retry-failed"
        count = args.next or args.retry_failed
        instance_ids = select_instances(args.benchmark, effective_model, mode, count)
        if not instance_ids:
            print(f"[benchmark] No instances matched --{mode.replace('-', '_')} for model={effective_model}")
            return
        print(f"[benchmark] Selected {len(instance_ids)} instances via --{mode}: {', '.join(instance_ids)}")
    else:
        instance_ids = None

    # Register signal handlers and atexit
    _cleanup_state["keep"] = args.keep
    signal.signal(signal.SIGINT, _signal_handler)
    signal.signal(signal.SIGTERM, _signal_handler)
    atexit.register(_atexit_teardown)
    print(f"==> Eval run: {resource_name}")
    print(f"    Benchmark: {args.benchmark}")
    print(f"    Model:     {effective_model}")
    print(f"    Profile:   {args.profile} ({profile['droplet_size']}, {profile['volume_gb']} GB)")
    print(f"    Region:    {args.region}")
    print()

    tmp_dir = tempfile.mkdtemp(prefix="scaffold-eval-")

    try:
        # 1. Create SSH key
        key_path, _, ssh_key_id = create_ssh_key(tmp_dir)
        _cleanup_state["ssh_key_id"] = ssh_key_id

        # 2. Create volume
        volume_id = create_volume(resource_name, profile["volume_gb"], args.region)
        _cleanup_state["volume_id"] = volume_id

        # 3. Create droplet
        droplet_id, ip = create_droplet(resource_name, args.profile, args.region, ssh_key_id)
        _cleanup_state["droplet_id"] = droplet_id

        # 4. Attach volume to droplet
        attach_volume(volume_id, droplet_id)

        # 5. Wait for SSH
        wait_for_ssh(ip, key_path)

        # 6. Setup droplet
        setup_droplet(ip, key_path, resource_name, scaffold_binary, evals_dir,
                      args.benchmark)

        # 7. Upload env file (keeps secrets out of process args)
        upload_env_file(ip, key_path, remote_env)

        # 8. Run benchmark (generate predictions) — always in debug mode
        run_benchmark(
            ip, key_path, args.benchmark, effective_model,
            instance_ids, args.max_instances, args.timeout,
            local_output_dir=args.output,
        )

        # 9. Score predictions
        run_evaluation(
            ip, key_path, args.benchmark, run_id,
            profile["ncpus"],
        )

        # 10. Retrieve results
        retrieve_results(ip, key_path, args.output)

        # 11. Update benchmark results
        update_benchmark_results(args.output, args.benchmark, effective_model, run_id)

        print(f"\n==> Eval complete. Results in {args.output}/")

    except Exception as e:
        print(f"\n[error] {e}", file=sys.stderr)

        # Try to retrieve partial results
        if _cleanup_state.get("droplet_id") and "ip" in dir():
            print("[error] Attempting to retrieve partial results...", file=sys.stderr)
            try:
                retrieve_results(ip, key_path, args.output)  # type: ignore[possibly-undefined]
            except Exception:
                print("[error] Could not retrieve partial results", file=sys.stderr)

        if not args.keep:
            _atexit_teardown()
        sys.exit(1)

    finally:
        if args.keep and "key_path" in dir():
            # Persist SSH key so the user can actually connect to the kept droplet
            import shutil
            persist_key = os.path.join(args.output, "eval_key")
            os.makedirs(args.output, exist_ok=True)
            shutil.copy2(key_path, persist_key)  # type: ignore[possibly-undefined]
            os.chmod(persist_key, 0o600)

        # Clean up temp dir
        import shutil
        shutil.rmtree(tmp_dir, ignore_errors=True)

    # Teardown (unless --keep)
    if not args.keep:
        teardown(droplet_id, volume_id, ssh_key_id)
        _cleanup_state["torn_down"] = True
    else:
        persist_key = os.path.join(args.output, "eval_key")
        print(f"\n[keep] Droplet kept alive: ssh -i {persist_key} root@{ip}")
        print(f"       To tear down manually:")
        print(f"         doctl compute droplet delete {droplet_id} --force")
        print(f"         doctl compute volume delete {volume_id} --force")
        print(f"         doctl compute ssh-key delete {ssh_key_id} --force")


if __name__ == "__main__":
    main()
