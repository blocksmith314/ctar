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
| 2026-11-22 | main | `0bdcdc5` | 3.537 s | 926.6 MiB | 4.343 s | 781.0 MiB |

A second, pack-only run on the same machine measured main at 3.632 s / 890.4 MiB
(v1.1.1: 3.559 s / 857.8 MiB). Pack peak RSS is therefore roughly 4-9% above
v1.1.1 across runs while wall time stays flat. This drift is tracked in #93 and
is likely related to the metadata compression buffers added after v1.1.1; treat
the v1.1.1 -> main memory row as a known open item, not a finished comparison.

## Updating

Before each release, run:

```shell
scripts/perf_gate.sh --dataset <tree> --ref <previous-tag> --runs 10 --mode both
```

Add one row for the release (current commit and measurement date), and note any
measurement caveats. Keep the dataset, thread count and machine consistent
between rows so the trend is comparable.
