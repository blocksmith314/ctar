# Benchmark history

Release-level pack/unpack numbers for `ctar`. The per-change gate is
`scripts/perf_gate.sh`; this file records the durable, release-to-release
history. A local `.cache/perf_gate_history.jsonl` is a per-machine cache and is
not part of the repository.

## Method

- Release build, `scripts/perf_gate.sh --ref <previous release tag> --runs 10 --mode both`.
- Dataset: a source tree with about 96k files and 2 GB of data.
- `-t 8 -c lz4`; hyperfine mean wall time; peak RSS reported by hyperfine.
- Numbers are machine-specific (macOS arm64 / Apple Silicon in the rows below).
  Compare rows measured on the same machine.

## History

| measured | release / ref | commit | pack time | pack peak RSS | unpack time | unpack peak RSS |
|---|---|---|---|---|---|---|
| 2026-11-22 | v1.1.1 | `603993d` | 3.557 s | 851.6 MiB | 4.442 s | 768.8 MiB |
| 2026-11-22 | main (pre-#93) | `0bdcdc5` | 3.537 s | 926.6 MiB | 4.343 s | 781.0 MiB |
| 2026-11-22 | main (post-#93) | `afdeb6b` | 3.61-3.74 s | 852-878 MiB | - | - |

A second pack-only run measured the pre-#93 main at 3.632 s / 890.4 MiB
(v1.1.1: 3.559 s / 857.8 MiB). #93 traced the ~4-9% pack-RSS drift to redundant
metadata copies during packing and fixed it in `afdeb6b`. After the fix, pack
peak RSS measured 852-878 MiB across three 10-run comparisons (v1.1.1 baseline:
838-854 MiB). The allocation reduction is visible in the code, but this
machine's run-to-run RSS noise is around +/-20 MiB; replace the post-#93 row
with the next release measurement.

## Updating

Before each release, run:

```shell
scripts/perf_gate.sh --dataset <tree> --ref <previous-tag> --runs 10 --mode both
```

Add one row for the release (current commit and measurement date), and note any
measurement caveats. Keep the dataset, thread count and machine consistent
between rows so the trend is comparable.
