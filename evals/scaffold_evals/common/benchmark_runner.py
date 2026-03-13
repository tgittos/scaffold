"""Common benchmark runner logic shared across swebench and feabench."""

from __future__ import annotations

import argparse
import json
import os
import sys
import traceback
import urllib.request
from pathlib import Path

from scaffold_evals.common.config import load_config
from scaffold_evals.common.instance_loader import load_instances
from scaffold_evals.common.patch_extractor import extract_patch_from_container
from scaffold_evals.common.scaffold_runner import run_scaffold_in_container

# Files to extract from scaffold's home dir inside the container
_SCAFFOLD_HOME_ARTIFACTS = ("scaffold.db", "config.json", "session.log")


# SWE-bench Docker namespace for pre-built images on Docker Hub
_SWEBENCH_NAMESPACE = "swebench"


def _fetch_pr_description(repo: str, pull_number: int) -> str:
    """Fetch PR title and body from GitHub API."""
    url = f"https://api.github.com/repos/{repo}/pulls/{pull_number}"
    req = urllib.request.Request(url)
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        req.add_header("Authorization", f"token {token}")
    req.add_header("Accept", "application/vnd.github.v3+json")
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = json.loads(resp.read())
            title = data.get("title", "")
            body = data.get("body", "") or ""
            return f"{title}\n\n{body}".strip()
    except Exception as e:
        print(f"[warning] Could not fetch PR #{pull_number} from {repo}: {e}", flush=True)
        return ""


def _prepare_feabench_instance(instance: dict) -> dict:
    """Add missing fields that make_test_spec expects for FEA-bench instances."""
    if "test_patch" in instance:
        return instance
    instance = dict(instance)
    instance["test_patch"] = ""
    if not instance.get("problem_statement"):
        repo = instance.get("repo", "")
        pull_number = instance.get("pull_number")
        if repo and pull_number:
            instance["problem_statement"] = _fetch_pr_description(repo, pull_number)
        else:
            instance["problem_statement"] = ""
    return instance


def run_patch_instance(
    instance: dict,
    scaffold_binary: str,
    model: str,
    workdir: Path,
    timeout: int,
    env_vars: dict[str, str],
    benchmark_name: str,
    scaffold_home: Path | None = None,
    debug: bool = False,
) -> dict:
    """Run scaffold on a benchmark instance inside a SWE-bench Docker container.

    The container has project dependencies pre-installed so the agent can
    run tests to verify its work. Returns a prediction dict.
    """
    instance_id = instance["instance_id"]
    print(f"[{benchmark_name}] Running {instance_id}...", flush=True)

    result, patch = _run_in_docker(
        instance, scaffold_binary, model, workdir, timeout,
        env_vars, scaffold_home, debug, benchmark_name,
    )

    print(
        f"[{benchmark_name}] {instance_id}: exit={result.exit_code}, "
        f"patch_lines={len(patch.splitlines())}",
        flush=True,
    )

    if result.exit_code != 0 and result.raw_stderr:
        print(f"[{benchmark_name}] {instance_id} stderr: {result.raw_stderr[:500]}", flush=True)

    # Save scaffold logs for debugging
    log_dir = workdir / "logs" / instance_id.replace("/", "__")
    log_dir.mkdir(parents=True, exist_ok=True)
    if result.raw_stdout:
        (log_dir / "stdout.log").write_text(result.raw_stdout)
    if result.raw_stderr:
        (log_dir / "stderr.log").write_text(result.raw_stderr)

    # Save patch as standalone file for easy inspection
    (log_dir / "patch.diff").write_text(patch)

    # Save the scaffold conversation as structured JSON for analysis
    if result.messages:
        (log_dir / "conversation.json").write_text(
            json.dumps(result.messages, indent=2)
        )

    return {
        "instance_id": instance_id,
        "model_name_or_path": model,
        "model_patch": patch,
    }


def _collect_container_diagnostics(container, benchmark_name: str) -> str:
    """Collect diagnostic info from the container before it's destroyed."""
    sections = []
    cmds = [
        ("git status", ["git", "status"]),
        ("git log -10 --oneline", ["git", "log", "--oneline", "-10"]),
        ("git diff --stat", ["git", "diff", "--stat"]),
        ("conda env list", ["conda", "env", "list"]),
        ("pip list (testbed)", ["bash", "-c",
         "/opt/miniconda3/envs/testbed/bin/pip list 2>/dev/null || echo 'N/A'"]),
        ("scaffold home listing", ["bash", "-c",
         "find /root/.local/scaffold/ -type f 2>/dev/null || echo 'empty'"]),
    ]
    for label, cmd in cmds:
        try:
            exit_code, output = container.exec_run(cmd, workdir="/testbed/")
            text = output.decode("utf-8", errors="replace") if isinstance(output, bytes) else output
            sections.append(f"=== {label} (exit={exit_code}) ===\n{text}")
        except Exception as e:
            sections.append(f"=== {label} (error) ===\n{e}")
    return "\n\n".join(sections)


def _extract_scaffold_home(container, log_dir: Path, benchmark_name: str) -> None:
    """Extract scaffold home artifacts from the container for post-mortem analysis."""
    home_dir = log_dir / "scaffold_home"
    home_dir.mkdir(parents=True, exist_ok=True)
    for artifact in _SCAFFOLD_HOME_ARTIFACTS:
        try:
            exit_code, output = container.exec_run(
                ["cat", f"/root/.local/scaffold/{artifact}"],
            )
            if exit_code == 0 and output:
                data = output if isinstance(output, bytes) else output.encode()
                (home_dir / artifact).write_bytes(data)
        except Exception:
            pass  # Non-fatal; best-effort extraction

    # Also grab any .log files in scaffold home
    try:
        exit_code, output = container.exec_run(
            ["find", "/root/.local/scaffold/", "-name", "*.log", "-type", "f"],
        )
        if exit_code == 0 and output:
            text = output.decode("utf-8", errors="replace") if isinstance(output, bytes) else output
            for log_path in text.strip().splitlines():
                log_path = log_path.strip()
                if not log_path:
                    continue
                name = log_path.rsplit("/", 1)[-1]
                try:
                    _, content = container.exec_run(["cat", log_path])
                    if content:
                        data = content if isinstance(content, bytes) else content.encode()
                        (home_dir / name).write_bytes(data)
                except Exception:
                    pass
    except Exception:
        pass


def _run_in_docker(instance, scaffold_binary, model, workdir, timeout,
                   env_vars, scaffold_home, debug, benchmark_name):
    """Run scaffold inside a SWE-bench Docker container."""
    import docker
    from swebench.harness.docker_build import build_instance_images
    from swebench.harness.docker_utils import (
        cleanup_container,
        copy_to_container,
    )
    from swebench.harness.test_spec.test_spec import make_test_spec

    instance_id = instance["instance_id"]
    client = docker.from_env()

    # FEA-bench instances lack test_patch/problem_statement; fill them in
    instance = _prepare_feabench_instance(instance)

    # Build/pull the instance image (base -> env -> instance, cached)
    spec = make_test_spec(instance, namespace=_SWEBENCH_NAMESPACE)

    # Try to pull the pre-built instance image from Docker Hub first.
    # SWE-bench publishes images under the "swebench" namespace, but their
    # build functions never pull — they build from scratch (which OOMs on
    # small droplets). If we can pull the instance image, we skip the build
    # entirely.
    image_ready = False
    try:
        client.images.get(spec.instance_image_key)
        image_ready = True
    except docker.errors.ImageNotFound:
        print(f"[{benchmark_name}] Pulling {spec.instance_image_key}...", flush=True)
        try:
            client.images.pull(spec.instance_image_key)
            image_ready = True
        except docker.errors.APIError as e:
            print(f"[{benchmark_name}] Pull failed ({e}), building locally...", flush=True)

    if not image_ready:
        print(f"[{benchmark_name}] Building image {spec.instance_image_key}...", flush=True)
        build_instance_images(
            client=client,
            dataset=[instance],
            namespace=_SWEBENCH_NAMESPACE,
            tag="latest",
            env_image_tag="latest",
            max_workers=1,
        )

    container = None
    try:
        # Start container
        container = client.containers.create(
            image=spec.instance_image_key,
            user="root",
            detach=True,
            command="tail -f /dev/null",
            platform=spec.platform,
        )
        container.start()
        print(f"[{benchmark_name}] Container {container.short_id} started", flush=True)

        # Copy scaffold binary into container
        scaffold_path = Path(scaffold_binary).resolve()
        copy_to_container(
            container, scaffold_path, Path("/usr/local/bin/scaffold")
        )
        container.exec_run("chmod +x /usr/local/bin/scaffold")

        # Gather repo orientation context
        repo_context = ""
        try:
            _, dir_raw = container.exec_run(
                ["find", "/testbed", "-maxdepth", "2", "-type", "d",
                 "-not", "-path", "*/.git/*"], demux=True)
            _, test_raw = container.exec_run(
                ["find", "/testbed", "-path", "*/test*", "-name", "*.py",
                 "-not", "-path", "*/.git/*"], demux=True)
            dir_text = (dir_raw[0] or b"").decode("utf-8", errors="replace")
            test_text = (test_raw[0] or b"").decode("utf-8", errors="replace")
            # Cap each to 50 lines
            dir_lines = "\n".join(dir_text.strip().splitlines()[:50])
            test_lines = "\n".join(test_text.strip().splitlines()[:50])
            if dir_lines or test_lines:
                repo_context = (
                    "<repo-context>\n"
                    f"Directory structure:\n{dir_lines}\n\n"
                    f"Test files:\n{test_lines}\n"
                    "</repo-context>\n\n"
                )
        except Exception:
            pass  # Non-fatal; proceed without context

        # Run scaffold
        issue_text = instance.get("problem_statement", "")
        fail_to_pass = instance.get("FAIL_TO_PASS", [])
        if isinstance(fail_to_pass, str):
            import json as _json
            fail_to_pass = _json.loads(fail_to_pass)

        is_feature = benchmark_name == "feabench"
        if is_feature:
            test_hint = ""
            if fail_to_pass:
                test_names = "\n".join(fail_to_pass[:20])
                test_hint = (
                    f"\n\nThe following tests must pass after your implementation:\n"
                    f"<tests>\n{test_names}\n</tests>\n"
                )
            message = (
                "Implement the following feature in this repository.\n\n"
                f"{repo_context}"
                f"<feature-request>\n{issue_text}\n</feature-request>"
                f"{test_hint}\n\n"
                "Read the failing tests carefully — they define the expected "
                "behavior. Explore the codebase to understand the existing "
                "patterns and conventions before writing code.\n\n"
                "After implementing, run the relevant test suite to verify. "
                "Your patch must pass all existing tests plus the new ones. "
                "If any test fails, iterate until all tests pass."
            )
        else:
            message = (
                "Resolve the following issue in this repository.\n\n"
                f"{repo_context}"
                f"<issue>\n{issue_text}\n</issue>\n\n"
                "The fix may not belong where the error appears. Before "
                "patching, check how the module handles analogous cases "
                "and make sure you are fixing the root cause, not "
                "suppressing a symptom. Read the tests for the affected "
                "code — they show intended behavior and edge cases.\n\n"
                "After making your fix, run the full test suite for the "
                "affected module — not just a single test file. Your "
                "patch must pass all existing tests. If any test fails, "
                "iterate on your fix until all tests pass."
            )

        # FEA-bench always runs in debug mode — the benchmark is newer and
        # we need full diagnostic output for every run.
        effective_debug = debug or is_feature

        result = run_scaffold_in_container(
            container=container,
            message=message,
            model=model,
            env_vars=env_vars,
            scaffold_home=scaffold_home,
            timeout=timeout,
            debug=effective_debug,
        )

        # Extract patch
        patch = extract_patch_from_container(container)

        # Collect artifacts before container cleanup
        log_dir = workdir / "logs" / instance_id.replace("/", "__")
        log_dir.mkdir(parents=True, exist_ok=True)

        # Container diagnostics (git state, env info)
        try:
            diag = _collect_container_diagnostics(container, benchmark_name)
            (log_dir / "container_diagnostics.log").write_text(diag)
        except Exception as e:
            print(f"[{benchmark_name}] Warning: diagnostics collection failed: {e}", flush=True)

        # Scaffold home artifacts (conversation DB, session logs)
        try:
            _extract_scaffold_home(container, log_dir, benchmark_name)
        except Exception as e:
            print(f"[{benchmark_name}] Warning: scaffold home extraction failed: {e}", flush=True)

        return result, patch
    finally:
        if container:
            cleanup_container(client, container, None)


def run_patch_benchmark(
    benchmark_name: str,
    default_dataset: str,
    default_timeout: int,
    default_workdir: str,
) -> None:
    """Main entry point for patch-based benchmarks (swebench/feabench)."""
    parser = argparse.ArgumentParser(
        description=f"Run {benchmark_name} evaluation with scaffold"
    )
    parser.add_argument("--scaffold-binary", default=None, help="Path to scaffold binary")
    parser.add_argument("--model", default=None, help="Model to evaluate")
    parser.add_argument("--output", default="predictions.jsonl", help="Output predictions JSONL")
    parser.add_argument("--workdir", default=default_workdir, help="Working directory for repos")
    parser.add_argument("--dataset", default=default_dataset, help="HuggingFace dataset name")
    parser.add_argument("--split", default="test", help="Dataset split")
    parser.add_argument("--instance-ids", nargs="*", help="Specific instance IDs to run")
    parser.add_argument("--max-instances", type=int, help="Maximum number of instances")
    parser.add_argument(
        "--timeout", type=int, default=default_timeout, help="Per-instance timeout in seconds"
    )
    parser.add_argument("--config", type=Path, help="TOML config file")
    parser.add_argument(
        "--debug", action="store_true",
        help="Run scaffold in debug mode (enables verbose logging)",
    )
    parser.add_argument(
        "--scaffold-home", type=Path,
        default=Path.home() / ".local" / "scaffold",
        help="Scaffold home dir to sync OAuth credentials from (default: ~/.local/scaffold)",
    )
    args = parser.parse_args()

    config = load_config(args.config)
    scaffold_binary = args.scaffold_binary if args.scaffold_binary is not None else config.scaffold_binary
    model = args.model if args.model is not None else config.model
    workdir = Path(args.workdir)
    workdir.mkdir(parents=True, exist_ok=True)
    scaffold_home = args.scaffold_home if any((args.scaffold_home / f).exists() for f in ("oauth2.db", "config.json")) else None

    instances = load_instances(
        args.dataset,
        split=args.split,
        instance_ids=args.instance_ids,
        max_instances=args.max_instances,
    )

    if not instances:
        print(f"[{benchmark_name}] No instances to run.", file=sys.stderr)
        sys.exit(1)

    print(f"[{benchmark_name}] Running {len(instances)} instances with model={model}", flush=True)

    output_path = Path(args.output)
    if output_path.exists():
        print(f"[{benchmark_name}] Warning: {output_path} exists, overwriting.", file=sys.stderr)
        output_path.write_text("")

    for instance in instances:
        try:
            prediction = run_patch_instance(
                instance=instance,
                scaffold_binary=scaffold_binary,
                model=model,
                workdir=workdir,
                timeout=args.timeout,
                env_vars=config.env_vars,
                benchmark_name=benchmark_name,
                scaffold_home=scaffold_home,
                debug=args.debug,
            )
            with open(output_path, "a") as f:
                f.write(json.dumps(prediction) + "\n")
        except Exception:
            print(
                f"[{benchmark_name}] ERROR on {instance.get('instance_id', '?')}:",
                file=sys.stderr,
            )
            traceback.print_exc()

    print(f"[{benchmark_name}] Done. Predictions written to {output_path}", flush=True)
