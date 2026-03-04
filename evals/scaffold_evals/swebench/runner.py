"""SWE-bench evaluation runner for scaffold."""

from scaffold_evals.common.benchmark_runner import run_patch_benchmark
from scaffold_evals.swebench.prompt import build_prompt


def main() -> None:
    run_patch_benchmark(
        benchmark_name="swebench",
        default_dataset="princeton-nlp/SWE-bench_Verified",
        default_timeout=600,
        default_workdir="/tmp/eval/swebench",
        message="Fix the issue described in your system prompt.",
        prompt_builder=build_prompt,
    )


if __name__ == "__main__":
    main()
