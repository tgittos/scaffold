#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# ///
"""Provision a DigitalOcean droplet, run scaffold evals, score them, retrieve results.

Supports swebench, feabench, and livecodebench benchmarks. Handles the full lifecycle:
  1. Create ephemeral SSH key + DO droplet + block storage volume
  2. Install Docker, uv, mount volume, copy scaffold binary + evals package
  3. Generate predictions via scaffold-eval-<benchmark>
  4. Score predictions via SWE-bench Docker evaluation harness
  5. SCP scored results back to local machine
  6. Tear down all DO resources

Usage:
  source .env && ./scripts/run_eval.py swebench -i django__django-16379
"""

from __future__ import annotations

import argparse
import atexit
import json
import math
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
    "dev": {"droplet_size": "s-4vcpu-8gb", "volume_gb": 50, "ncpus": 4},
}

SUPPORTED_BENCHMARKS = ("swebench", "feabench", "livecodebench")
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


PERSISTENT_VOLUME_NAME = "scaffold-eval-cache"


def get_or_create_volume(size_gb: int, region: str) -> tuple[str, bool]:
    """Get existing persistent volume or create one. Returns (vol_id, is_new)."""
    vol_name = f"{PERSISTENT_VOLUME_NAME}-{region}"

    # Check for existing volume
    out = doctl("compute", "volume", "list", "--region", region)
    volumes = json.loads(out)
    for vol in volumes:
        if vol["name"] == vol_name:
            vol_id = vol["id"]
            print(f"[volume] Reusing {vol_name} id={vol_id}")
            return vol_id, False

    # Create new
    out = doctl(
        "compute", "volume", "create", vol_name,
        "--region", region,
        "--size", f"{size_gb}GiB",
        "--fs-type", "ext4",
    )
    vol = json.loads(out)
    vol_id = vol[0]["id"]
    print(f"[volume] Created {vol_name} ({size_gb} GB) id={vol_id}")
    return vol_id, True


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
        # Wait for the volume device node to appear (may lag after attach)
        f"for i in $(seq 1 30); do [ -e {vol_dev} ] && break; echo 'Waiting for volume device...'; sleep 2; done && "
        f"[ -e {vol_dev} ] || {{ echo 'Volume device {vol_dev} not found'; exit 1; }} && "
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

    if benchmark == "feabench":
        print("[setup] Applying FEA-bench patches to swebench...")
        ssh_run(ip, key_path, (
            "export PATH=\"$HOME/.local/bin:$PATH\" && "
            "cd /root/evals && "
            "uv run python -m scaffold_evals.feabench.patch_swebench"
        ))

    print("[setup] Droplet ready")


# ---------------------------------------------------------------------------
# Artifact cleanup
# ---------------------------------------------------------------------------


def clean_stale_artifacts(
    ip: str,
    key_path: str,
    instance_ids: list[str] | None,
) -> None:
    """Remove stale predictions, logs, and eval artifacts from the persistent volume.

    This ensures re-runs of the same instance IDs produce fresh results
    instead of reusing cached artifacts from prior runs.
    """
    print("\n[cleanup] Removing stale artifacts from persistent volume...")

    # Always remove the old predictions file so benchmark_runner starts fresh
    cmds = [f"rm -f {MOUNT_POINT}/predictions.jsonl"]

    if instance_ids:
        for iid in instance_ids:
            safe_id = iid.replace("/", "__")
            # Scaffold work logs (conversation.json, patch.diff, stdout.log)
            cmds.append(f"rm -rf {MOUNT_POINT}/work/logs/{safe_id}")
            # Eval scoring results
            cmds.append(f"rm -rf {MOUNT_POINT}/results/*{safe_id}* 2>/dev/null || true")
        # SWE-bench eval logs on root disk
        cmds.append("rm -rf /root/evals/logs/ 2>/dev/null || true")
    else:
        # No specific instances — full clean of all run artifacts
        cmds.append(f"rm -rf {MOUNT_POINT}/work/logs")
        cmds.append(f"rm -rf {MOUNT_POINT}/results")
        cmds.append("rm -rf /root/evals/logs/ 2>/dev/null || true")

    ssh_run(ip, key_path, " && ".join(cmds), check=False)
    print("[cleanup] Done")


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
    run_id: str = "",
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

    sessions_dir = os.path.join(local_output_dir, "sessions")
    os.makedirs(sessions_dir, exist_ok=True)
    session_log = os.path.join(sessions_dir, f"session_{run_id}.log") if run_id else os.path.join(local_output_dir, "session.log")
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

    # Copy swebench summary report to results dir for retrieve_results
    # swebench writes {model}.{run_id}.json to cwd (/root/evals/)
    ssh_run(ip, key_path, (
        f"mkdir -p {MOUNT_POINT}/results && "
        f"cp /root/evals/*.{run_id}.json {MOUNT_POINT}/results/ 2>/dev/null || true"
    ))
    print("[eval] Scoring complete")


def run_livecodebench_evaluation(
    ip: str,
    key_path: str,
    run_id: str,
) -> None:
    """Score LiveCodeBench predictions by running code against test cases."""
    print(f"\n[eval] Scoring LiveCodeBench predictions (run_id={run_id})...")

    cmd = (
        f"export PATH=\"$HOME/.local/bin:$PATH\" && "
        f"cd /root/evals && "
        f"uv run python -m scaffold_evals.livecodebench.evaluate "
        f"--predictions {MOUNT_POINT}/predictions.jsonl "
        f"--output {MOUNT_POINT}/results/{run_id}.json"
    )

    ssh_run(ip, key_path, cmd)
    print("[eval] Scoring complete")


def _merge_predictions(existing_path: str, new_path: str) -> None:
    """Merge new predictions into existing predictions.jsonl, replacing by instance_id."""
    existing = {}
    if os.path.isfile(existing_path):
        with open(existing_path) as f:
            for line in f:
                line = line.strip()
                if line:
                    entry = json.loads(line)
                    existing[entry["instance_id"]] = entry

    with open(new_path) as f:
        for line in f:
            line = line.strip()
            if line:
                entry = json.loads(line)
                existing[entry["instance_id"]] = entry

    with open(existing_path, "w") as f:
        for entry in existing.values():
            f.write(json.dumps(entry) + "\n")


def _distribute_per_instance(staging_dir: str, instances_dir: str, subdir: str) -> None:
    """Move files from a flat staging directory into per-instance subdirectories."""
    import shutil

    staging = Path(staging_dir)
    if not staging.is_dir():
        return
    for item in staging.iterdir():
        if item.is_dir():
            instance_id = item.name
            dest = Path(instances_dir) / instance_id / subdir
            dest.mkdir(parents=True, exist_ok=True)
            # Overwrite existing files for this instance
            for f in item.iterdir():
                target = dest / f.name
                if f.is_dir():
                    if target.exists():
                        shutil.rmtree(target)
                    shutil.copytree(f, target)
                else:
                    shutil.copy2(f, target)


def retrieve_results(
    ip: str,
    key_path: str,
    local_output_dir: str,
    run_id: str,
    run_index: int | None = None,
) -> None:
    """Download predictions and evaluation results from the droplet.

    Organizes output per-instance so previous results are preserved.
    Only re-run instances get overwritten.

    When run_index is set (multi-run mode), artifacts are stored under
    instances/{id}/runs/run_{N}/scaffold/ and .../eval/ instead of
    directly under scaffold/ and eval/.

    Layout:
        {local_output_dir}/
            predictions.jsonl           — merged across all runs
            sessions/
                session_{run_id}.log
            instances/
                {instance_id}/
                    scaffold/           — single-run artifacts (run_index=None)
                    eval/
                    runs/               — multi-run artifacts (run_index set)
                        run_1/scaffold/
                        run_1/eval/
            _staging/                   — ephemeral, for update_benchmark_results
                results/
                eval_logs/
    """
    import shutil

    os.makedirs(local_output_dir, exist_ok=True)

    staging = os.path.join(local_output_dir, "_staging")
    if os.path.isdir(staging):
        shutil.rmtree(staging)
    os.makedirs(staging, exist_ok=True)

    instances_dir = os.path.join(local_output_dir, "instances")
    os.makedirs(instances_dir, exist_ok=True)

    print(f"\n[results] Retrieving results to {local_output_dir}/...")

    # Predictions JSONL — download to staging, then merge
    new_predictions = os.path.join(staging, "predictions.jsonl")
    scp_from(
        ip, key_path,
        f"{MOUNT_POINT}/predictions.jsonl",
        new_predictions,
    )
    merged_predictions = os.path.join(local_output_dir, "predictions.jsonl")
    _merge_predictions(merged_predictions, new_predictions)

    # Evaluation results directory (for update_benchmark_results)
    rc = ssh_run(
        ip, key_path,
        f"test -d {MOUNT_POINT}/results && ls {MOUNT_POINT}/results/",
        check=False,
    )
    if rc == 0:
        scp_from(
            ip, key_path,
            f"{MOUNT_POINT}/results/",
            os.path.join(staging, "results"),
        )

    # Determine per-instance subdir based on run_index (multi-run vs single)
    scaffold_subdir = f"runs/run_{run_index}/scaffold" if run_index else "scaffold"
    eval_subdir = f"runs/run_{run_index}/eval" if run_index else "eval"

    # Scaffold per-instance logs (stdout/stderr) — distribute per instance
    scaffold_staging = os.path.join(staging, "scaffold_logs")
    rc = ssh_run(
        ip, key_path,
        f"test -d {MOUNT_POINT}/work/logs && ls {MOUNT_POINT}/work/logs/",
        check=False,
    )
    if rc == 0:
        scp_from(
            ip, key_path,
            f"{MOUNT_POINT}/work/logs/",
            scaffold_staging,
        )
        _distribute_per_instance(scaffold_staging, instances_dir, scaffold_subdir)

    # SWE-bench evaluation output dirs — distribute per instance
    eval_staging = os.path.join(staging, "eval_logs")
    rc = ssh_run(
        ip, key_path,
        f"ls /root/evals/logs/ 2>/dev/null",
        check=False,
    )
    if rc == 0:
        scp_from(
            ip, key_path,
            "/root/evals/logs/",
            eval_staging,
        )
        # Distribute per-instance report.json files
        run_eval_dir = Path(eval_staging) / "run_evaluation"
        if run_eval_dir.is_dir():
            _distribute_per_instance(str(run_eval_dir), instances_dir, eval_subdir)

    # Session log — save per run_id
    sessions_dir = os.path.join(local_output_dir, "sessions")
    os.makedirs(sessions_dir, exist_ok=True)
    old_session = os.path.join(local_output_dir, "session.log")
    if os.path.isfile(old_session):
        shutil.move(old_session, os.path.join(sessions_dir, f"session_{run_id}.log"))

    print(f"[results] Saved to {local_output_dir}/")


# ---------------------------------------------------------------------------
# Teardown
# ---------------------------------------------------------------------------


def teardown(
    droplet_id: str | None,
    volume_id: str | None,
    ssh_key_id: str | None,
) -> None:
    """Destroy droplet and SSH key. Volume is persistent and kept."""
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

    # Volume is persistent — don't destroy it
    if volume_id:
        print(f"[teardown] Volume {volume_id} kept (persistent cache)")

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


def _migrate_result_entry(entry: dict) -> dict:
    """Migrate old scalar {date, result} to new {runs: [...]} format."""
    if "runs" in entry:
        return entry
    # Old format: {"date": "...", "result": "..."}
    if "date" in entry and "result" in entry:
        return {"runs": [{"date": entry["date"], "result": entry["result"]}]}
    return entry


def load_results() -> dict:
    """Load benchmarks/results.json, migrating old scalar entries to runs arrays."""
    if not RESULTS_PATH.exists():
        return {}
    data = json.loads(RESULTS_PATH.read_text())
    for benchmark in data.values():
        for instance_id, instance_data in benchmark.items():
            for model in list(instance_data.keys()):
                instance_data[model] = _migrate_result_entry(instance_data[model])
    return data


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
    filter_prefix: str | None = None,
) -> list[str]:
    """Select instances based on mode ('next' or 'retry-failed').

    Returns up to `count` instance IDs from the registry that match the mode.
    If filter_prefix is given, only considers instances whose ID starts with it.
    """
    registry = load_registry(benchmark)
    results = load_results().get(benchmark, {})

    selected = []
    for instance_id in registry:
        if len(selected) >= count:
            break
        if filter_prefix and not instance_id.startswith(filter_prefix):
            continue
        instance_results = results.get(instance_id, {})
        model_result = instance_results.get(model)

        if mode == "next" and (model_result is None or not model_result.get("runs")):
            selected.append(instance_id)
        elif mode == "retry-failed" and model_result and model_result.get("runs"):
            if any(r.get("result") == "fail" for r in model_result["runs"]):
                selected.append(instance_id)

    return selected


def _parse_eval_report(
    output_dir: str, run_id: str,
) -> tuple[set[str], set[str]]:
    """Parse eval report for a single run, returning (resolved, unresolved) instance IDs."""
    resolved: set[str] = set()
    unresolved: set[str] = set()

    # Strategy 1: Look for a summary report in _staging/results/
    results_dir = Path(output_dir) / "_staging" / "results"
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
            for key in ("empty_patch_ids", "error_ids"):
                unresolved.update(report.get(key, []))
            unresolved.update(
                set(report.get("completed_ids", [])) - resolved
            )

    # Strategy 2: Aggregate per-instance report.json files from _staging/eval_logs
    if not resolved and not unresolved:
        eval_logs = Path(output_dir) / "_staging" / "eval_logs" / "run_evaluation"
        if eval_logs.exists():
            for report_file in eval_logs.rglob("report.json"):
                report = json.loads(report_file.read_text())
                for iid, info in report.items():
                    if info.get("resolved"):
                        resolved.add(iid)
                    else:
                        unresolved.add(iid)

    return resolved, unresolved


def update_benchmark_results(
    output_dir: str, benchmark: str, model: str, run_ids: list[str],
) -> None:
    """Parse eval reports and merge results into benchmarks/results.json.

    Each run_id corresponds to a separate eval run. Results are appended to the
    runs array for each (instance, model) pair.
    """
    all_results = load_results()
    bench_results = all_results.setdefault(benchmark, {})
    today = date.today().isoformat()
    total_new = 0

    for rid in run_ids:
        resolved, unresolved = _parse_eval_report(output_dir, rid)
        if not resolved and not unresolved:
            print(f"[benchmark] No report file found matching run_id={rid}")
            continue

        for iid in resolved:
            model_data = bench_results.setdefault(iid, {}).setdefault(model, {"runs": []})
            model_data["runs"].append({"date": today, "result": "pass", "run_id": rid})
        for iid in unresolved:
            model_data = bench_results.setdefault(iid, {}).setdefault(model, {"runs": []})
            model_data["runs"].append({"date": today, "result": "fail", "run_id": rid})

        new_count = len(resolved) + len(unresolved)
        total_new += new_count
        print(f"[benchmark] Run {rid}: {len(resolved)} pass, {len(unresolved)} fail")

    if total_new > 0:
        save_results(all_results)
        print(f"[benchmark] Updated results.json ({total_new} new run entries)")
        render_scorecard(all_results)


def _wilson_ci(passes: int, total: int, z: float = 1.96) -> tuple[float, float]:
    """Wilson score confidence interval for a binomial proportion.

    Returns (lower, upper) as fractions in [0, 1].
    """
    if total == 0:
        return (0.0, 0.0)
    p = passes / total
    denom = 1 + z * z / total
    centre = p + z * z / (2 * total)
    spread = z * math.sqrt((p * (1 - p) + z * z / (4 * total)) / total)
    return (max(0.0, (centre - spread) / denom), min(1.0, (centre + spread) / denom))


def _normal_cdf(x: float) -> float:
    """Standard normal CDF using math.erf."""
    return 0.5 * (1.0 + math.erf(x / math.sqrt(2.0)))


def _two_proportion_ztest(
    p1: float, n1: int, p2: float, n2: int,
) -> float:
    """Two-proportion z-test. Returns two-tailed p-value."""
    if n1 == 0 or n2 == 0:
        return 1.0
    p_pool = (p1 * n1 + p2 * n2) / (n1 + n2)
    if p_pool == 0.0 or p_pool == 1.0:
        return 1.0
    se = math.sqrt(p_pool * (1 - p_pool) * (1.0 / n1 + 1.0 / n2))
    if se == 0:
        return 1.0
    z = (p1 - p2) / se
    return 2.0 * (1.0 - _normal_cdf(abs(z)))


def _instance_pass_rate(model_data: dict) -> tuple[int, int]:
    """Return (passes, total_runs) from a model's data for an instance."""
    runs = model_data.get("runs", [])
    passes = sum(1 for r in runs if r.get("result") == "pass")
    return passes, len(runs)


def _instance_solved(model_data: dict) -> bool:
    """Return True if the instance has at least one passing run."""
    passes, total = _instance_pass_rate(model_data)
    return total > 0 and passes > 0


def render_scorecard(results: dict | None = None) -> None:
    """Generate BENCHMARKS.md from results.json + instance registries."""
    if results is None:
        results = load_results()
    benchmark_order = ["swebench", "feabench", "livecodebench"]
    benchmark_labels = {
        "swebench": "SWE-bench Verified",
        "feabench": "FEA-Bench",
        "livecodebench": "LiveCodeBench",
    }
    ordered_benchmarks = [b for b in benchmark_order if b in results]
    ordered_benchmarks += sorted(b for b in results if b not in benchmark_order)

    has_single_run = False

    lines = [
        "# Benchmark Scorecard",
        "",
        "*Generated by `scripts/run_eval.py --render`. Do not edit manually.*",
        "",
        "## Summary",
        "",
        "| Benchmark | Model | Solved | Tested | Pass Rate | 95% CI |",
        "|-----------|-------|--------|--------|-----------|--------|",
    ]

    for benchmark in ordered_benchmarks:
        bench_results = results[benchmark]
        registry = load_registry(benchmark) if (INSTANCES_DIR / f"{benchmark}.txt").exists() else []
        total_instances = len(registry)
        label = benchmark_labels.get(benchmark, benchmark)
        models = sorted({
            model
            for instance_data in bench_results.values()
            for model in instance_data
        })
        for model in models:
            solved_count = 0
            tested_count = 0
            for idata in bench_results.values():
                md = idata.get(model)
                if md and md.get("runs"):
                    tested_count += 1
                    if _instance_solved(md):
                        solved_count += 1
            if tested_count > 0:
                rate = 100.0 * solved_count / tested_count
                lo, hi = _wilson_ci(solved_count, tested_count)
                ci_str = f"{100*lo:.1f}%-{100*hi:.1f}%"
            else:
                rate = 0.0
                ci_str = "N/A"
            lines.append(
                f"| {label} | {model} | {solved_count} | {tested_count}/{total_instances} | {rate:.1f}% | {ci_str} |"
            )

    lines.append("")

    for benchmark in ordered_benchmarks:
        bench_results = results[benchmark]
        registry = load_registry(benchmark) if (INSTANCES_DIR / f"{benchmark}.txt").exists() else []
        total_instances = len(registry)

        models = sorted({
            model
            for instance_data in bench_results.values()
            for model in instance_data
        })

        if not models:
            continue

        benchmark_label = benchmark_labels.get(benchmark, benchmark)

        lines.append(f"## {benchmark_label} ({total_instances} instances)")
        lines.append("")

        # Summary table
        lines.append("### Summary")
        lines.append("| Model | Solved | Tested | Pass Rate | 95% CI |")
        lines.append("|-------|--------|--------|-----------|--------|")

        for model in models:
            solved_count = 0
            tested_count = 0
            total_runs = 0
            for idata in bench_results.values():
                md = idata.get(model)
                if md and md.get("runs"):
                    tested_count += 1
                    total_runs += len(md["runs"])
                    if _instance_solved(md):
                        solved_count += 1
            pending = total_instances - tested_count
            if tested_count > 0:
                rate = 100.0 * solved_count / tested_count
                lo, hi = _wilson_ci(solved_count, tested_count)
                ci_str = f"{100*lo:.1f}%-{100*hi:.1f}%"
            else:
                rate = 0.0
                ci_str = "N/A"
            lines.append(
                f"| {model} | {solved_count}/{tested_count} | {tested_count}/{total_instances} ({pending} pending, {total_runs} total runs) | {rate:.1f}% | {ci_str} |"
            )

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
                    md = bench_results[iid].get(model)
                    if md is None or not md.get("runs"):
                        cells.append("")
                    else:
                        passes, total = _instance_pass_rate(md)
                        solved = passes > 0
                        if total == 1:
                            has_single_run = True
                            icon = ":white_check_mark:" if solved else ":x:"
                            cells.append(f"{icon} 1/1*")
                        else:
                            icon = ":white_check_mark:" if passes == total else \
                                   ":large_orange_diamond:" if solved else ":x:"
                            cells.append(f"{icon} {passes}/{total}")
                lines.append(f"| {iid} | " + " | ".join(cells) + " |")

            lines.append("")

        # Instance registry (collapsible)
        if registry:
            lines.append("### Instance Registry")
            lines.append(f"<details><summary>All {total_instances} valid instance IDs</summary>")
            lines.append("")
            for iid in registry:
                lines.append(f"- {iid}")
            lines.append("")
            lines.append("</details>")
            lines.append("")

    if has_single_run:
        lines.insert(3, "")
        lines.insert(4, r"\* *Single-run result — not statistically reliable. Re-run with `--runs 5` for meaningful data.*")

    SCORECARD_PATH.write_text("\n".join(lines))
    print(f"[benchmark] Generated {SCORECARD_PATH} ({len(lines)} lines)")


def render_comparison(benchmark: str, model_a: str, model_b: str) -> None:
    """Print a statistical comparison of two models to stdout."""
    results = load_results()
    bench = results.get(benchmark, {})
    if not bench:
        print(f"No results for benchmark '{benchmark}'", file=sys.stderr)
        sys.exit(1)

    # Aggregate at instance level (solved = at least one pass)
    a_solved, a_tested = 0, 0
    b_solved, b_tested = 0, 0
    rows: list[tuple[str, str, str, str]] = []  # (iid, a_cell, b_cell, delta)

    all_ids = sorted(set(
        iid for iid in bench
        if bench[iid].get(model_a, {}).get("runs") or bench[iid].get(model_b, {}).get("runs")
    ))

    for iid in all_ids:
        a_data = bench[iid].get(model_a, {})
        b_data = bench[iid].get(model_b, {})
        ap, at = _instance_pass_rate(a_data) if a_data.get("runs") else (0, 0)
        bp, bt = _instance_pass_rate(b_data) if b_data.get("runs") else (0, 0)
        if at > 0:
            a_tested += 1
            if ap > 0:
                a_solved += 1
        if bt > 0:
            b_tested += 1
            if bp > 0:
                b_solved += 1

        a_cell = f"{ap}/{at}" if at > 0 else "-"
        b_cell = f"{bp}/{bt}" if bt > 0 else "-"
        if at > 0 and bt > 0:
            a_s = 1 if ap > 0 else 0
            b_s = 1 if bp > 0 else 0
            delta_val = (a_s - b_s) * 100
            delta = f"{delta_val:+.0f}pp" if delta_val != 0 else "="
        else:
            delta = "-"
        rows.append((iid, a_cell, b_cell, delta))

    # Summary
    print(f"\n{'='*60}")
    print(f"Comparison: {model_a} vs {model_b} ({benchmark})")
    print(f"{'='*60}\n")

    for label, solved, tested in [(model_a, a_solved, a_tested), (model_b, b_solved, b_tested)]:
        if tested > 0:
            rate = 100.0 * solved / tested
            lo, hi = _wilson_ci(solved, tested)
            print(f"  {label}: {rate:.1f}% ({solved}/{tested} instances solved) — 95% CI: [{100*lo:.1f}%, {100*hi:.1f}%]")
        else:
            print(f"  {label}: no data")

    # Z-test on instance-level proportions
    if a_tested > 0 and b_tested > 0:
        p_val = _two_proportion_ztest(a_solved / a_tested, a_tested, b_solved / b_tested, b_tested)
        sig = "YES" if p_val < 0.05 else "no"
        print(f"\n  z-test p-value: {p_val:.4f} — significant at 0.05? {sig}")
    print()

    # Per-instance delta table
    print(f"{'Instance':<45} {model_a:>10} {model_b:>10} {'Delta':>8}")
    print("-" * 75)
    for iid, a_cell, b_cell, delta in rows:
        print(f"{iid:<45} {a_cell:>10} {b_cell:>10} {delta:>8}")
    print()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run scaffold evals on a DigitalOcean droplet",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  ./scripts/run_eval.py swebench -i django__django-16379\n"
            "  ./scripts/run_eval.py feabench -m gpt-4o\n"
            "  ./scripts/run_eval.py swebench -n 50 --runs 3\n"
        ),
    )
    parser.add_argument(
        "benchmark",
        choices=SUPPORTED_BENCHMARKS,
        help="Benchmark to run (swebench, feabench, livecodebench)",
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
        "--filter",
        metavar="PREFIX",
        help="Filter instances by ID prefix (e.g. 'django', 'astropy__astropy'). Used with --next/--retry-failed.",
    )
    parser.add_argument(
        "--render",
        action="store_true",
        help="Regenerate BENCHMARKS.md from current results (no eval run)",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=5,
        metavar="N",
        help="Number of runs per eval (default: 5). Use --runs 1 for a quick check.",
    )
    parser.add_argument(
        "--compare",
        nargs=2,
        metavar=("MODEL_A", "MODEL_B"),
        help="Compare two models statistically and print results (no eval run)",
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

    # --compare: statistical comparison and exit
    if args.compare:
        render_comparison(args.benchmark, args.compare[0], args.compare[1])
        return

    # Validate mutual exclusivity of instance selection flags
    selection_flags = sum(1 for f in [args.instance_ids, args.next, args.retry_failed] if f)
    if selection_flags > 1:
        print("Error: --next, --retry-failed, and -i are mutually exclusive.", file=sys.stderr)
        sys.exit(1)

    if args.filter and not (args.next or args.retry_failed):
        print("Error: --filter requires --next or --retry-failed.", file=sys.stderr)
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
    num_runs = args.runs

    # Resolve instance selection
    if args.instance_ids:
        instance_ids = args.instance_ids.split(",")
    elif args.next or args.retry_failed:
        mode = "next" if args.next else "retry-failed"
        count = args.next or args.retry_failed
        instance_ids = select_instances(args.benchmark, effective_model, mode, count, args.filter)
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
    print(f"    Runs:      {num_runs}")
    print(f"    Profile:   {args.profile} ({profile['droplet_size']}, {profile['volume_gb']} GB)")
    print(f"    Region:    {args.region}")
    print()

    tmp_dir = tempfile.mkdtemp(prefix="scaffold-eval-")

    try:
        # 1. Create SSH key
        key_path, _, ssh_key_id = create_ssh_key(tmp_dir)
        _cleanup_state["ssh_key_id"] = ssh_key_id

        # 2. Get or create persistent volume
        volume_id, _vol_new = get_or_create_volume(profile["volume_gb"], args.region)
        _cleanup_state["volume_id"] = volume_id

        # 3. Create droplet
        droplet_id, ip = create_droplet(resource_name, args.profile, args.region, ssh_key_id)
        _cleanup_state["droplet_id"] = droplet_id

        # 4. Attach volume to droplet
        attach_volume(volume_id, droplet_id)

        # 5. Wait for SSH
        wait_for_ssh(ip, key_path)

        # 6. Setup droplet
        vol_name = f"{PERSISTENT_VOLUME_NAME}-{args.region}"
        setup_droplet(ip, key_path, vol_name, scaffold_binary, evals_dir,
                      args.benchmark)

        # 7. Upload env file (keeps secrets out of process args)
        upload_env_file(ip, key_path, remote_env)

        # Multi-run loop (steps 7.5–10 repeated per run)
        all_run_ids: list[str] = []
        for run_idx in range(1, num_runs + 1):
            if num_runs > 1:
                iter_run_id = f"{run_id}_run{run_idx}"
                print(f"\n{'='*60}")
                print(f"  Run {run_idx}/{num_runs} — {iter_run_id}")
                print(f"{'='*60}")
            else:
                iter_run_id = run_id

            # 7.5. Clean stale artifacts from previous runs on the persistent volume
            clean_stale_artifacts(ip, key_path, instance_ids)

            # 8. Run benchmark (generate predictions) — always in debug mode
            run_benchmark(
                ip, key_path, args.benchmark, effective_model,
                instance_ids, args.max_instances, args.timeout,
                local_output_dir=args.output,
                run_id=iter_run_id,
            )

            # 9. Score predictions
            if args.benchmark in EVAL_DATASETS:
                run_evaluation(
                    ip, key_path, args.benchmark, iter_run_id,
                    profile["ncpus"],
                )
            elif args.benchmark == "livecodebench":
                run_livecodebench_evaluation(ip, key_path, iter_run_id)

            # 10. Retrieve results
            if num_runs > 1:
                retrieve_results(ip, key_path, args.output, iter_run_id, run_index=run_idx)
            else:
                retrieve_results(ip, key_path, args.output, iter_run_id)

            all_run_ids.append(iter_run_id)

        # 11. Update benchmark results (all runs at once)
        update_benchmark_results(args.output, args.benchmark, effective_model, all_run_ids)

        print(f"\n==> Eval complete ({num_runs} run{'s' if num_runs > 1 else ''}). Results in {args.output}/")

    except Exception as e:
        print(f"\n[error] {e}", file=sys.stderr)

        # Try to retrieve partial results
        if _cleanup_state.get("droplet_id") and "ip" in dir():
            print("[error] Attempting to retrieve partial results...", file=sys.stderr)
            try:
                retrieve_results(ip, key_path, args.output, run_id)  # type: ignore[possibly-undefined]
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
        print(f"         doctl compute ssh-key delete {ssh_key_id} --force")
        print(f"       Volume {volume_id} is persistent (shared across runs)")


if __name__ == "__main__":
    main()
