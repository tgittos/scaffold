"""FEA-Bench evaluation runner for scaffold."""

from scaffold_evals.common.benchmark_runner import run_patch_benchmark
from scaffold_evals.feabench.prompt import build_prompt


def main() -> None:
    run_patch_benchmark(
        benchmark_name="feabench",
        default_dataset="microsoft/FEA-Bench",
        default_timeout=900,
        default_workdir="/tmp/eval/feabench",
        message="Implement the feature described in your system prompt.",
        prompt_builder=build_prompt,
    )


if __name__ == "__main__":
    main()
