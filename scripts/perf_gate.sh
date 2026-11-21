#!/usr/bin/env bash
# Fixed pack/unpack performance gate for ctar.
#
# The script only accepts a dataset path plus numeric or enumerated options. It
# never accepts a command to run, so allowlisting it grants a fixed benchmark
# workflow rather than arbitrary command execution.

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp_root=/tmp/ctar-perf
baseline_src="$tmp_root/baseline-src"
baseline_build="$tmp_root/baseline-build"
current_build="$repo_root/build-perf"
current_bin="$current_build/ctar"
baseline_bin="$baseline_build/ctar"

dataset=""
runs=5
mode=both
ref=origin/main
threads=8
max_regression=5.0
clean=0

usage() {
    cat <<'EOF'
Usage: scripts/perf_gate.sh --dataset DIR [options]

  --dataset DIR          Tree to pack (required, read-only)
  --runs N               hyperfine runs per command (1..20, default 5)
  --mode MODE            pack, unpack or both (default both)
  --ref REF              Baseline git ref (default origin/main)
  --threads N            Passed to ctar as -t (1..64, default 8)
  --max-regression PCT   Fail when time or peak RSS regresses by more
                         than PCT percent (default 5)
  --clean                Remove the baseline worktree and temp files
  -h, --help             Show this help

The script builds the current tree and a cached baseline from --ref in
Release mode, then compares `ctar pack` and `ctar unpack` with hyperfine.
All artifacts stay under /tmp/ctar-perf; the dataset is never modified.
EOF
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

quote() {
    printf '%q' "$1"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --dataset)
            [ $# -ge 2 ] || die "--dataset needs a value"
            dataset=$2
            shift 2
            ;;
        --runs)
            [ $# -ge 2 ] || die "--runs needs a value"
            runs=$2
            shift 2
            ;;
        --mode)
            [ $# -ge 2 ] || die "--mode needs a value"
            mode=$2
            shift 2
            ;;
        --ref)
            [ $# -ge 2 ] || die "--ref needs a value"
            ref=$2
            shift 2
            ;;
        --threads)
            [ $# -ge 2 ] || die "--threads needs a value"
            threads=$2
            shift 2
            ;;
        --max-regression)
            [ $# -ge 2 ] || die "--max-regression needs a value"
            max_regression=$2
            shift 2
            ;;
        --clean)
            clean=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

[ -n "$dataset" ] || die "--dataset is required"
[ -d "$dataset" ] || die "dataset is not a directory: $dataset"
case "$mode" in
    pack|unpack|both) ;;
    *) die "--mode must be pack, unpack or both" ;;
esac
case "$runs" in
    ''|*[!0-9]*) die "--runs must be an integer" ;;
esac
[ "$runs" -ge 1 ] && [ "$runs" -le 20 ] || die "--runs must be between 1 and 20"
if [ "$runs" -lt 5 ]; then
    printf 'warning: fewer than 5 runs can produce noisy results\n' >&2
fi
case "$threads" in
    ''|*[!0-9]*) die "--threads must be an integer" ;;
esac
[ "$threads" -ge 1 ] && [ "$threads" -le 64 ] || die "--threads must be between 1 and 64"
awk -v v="$max_regression" 'BEGIN { exit (v ~ /^[0-9]+([.][0-9]+)?$/ ? 0 : 1) }' \
    || die "--max-regression must be a non-negative number"
case "$dataset" in
    *$'\n'*) die "dataset path must not contain a newline" ;;
esac

for tool in cmake ninja hyperfine jq git; do
    command -v "$tool" >/dev/null 2>&1 || die "required tool not found: $tool"
done

baseline_commit=$(git -C "$repo_root" rev-parse --verify "$ref^{commit}" 2>/dev/null) \
    || die "cannot resolve ref: $ref"
current_commit=$(git -C "$repo_root" rev-parse --verify HEAD)
mkdir -p "$tmp_root"

build_current() {
    cmake -S "$repo_root" -B "$current_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TEST=OFF -DBUILD_CLI=ON \
        >"$tmp_root/configure-current.log" 2>&1 \
        || { cat "$tmp_root/configure-current.log" >&2; die "current configure failed"; }
    cmake --build "$current_build" >"$tmp_root/build-current.log" 2>&1 \
        || { cat "$tmp_root/build-current.log" >&2; die "current build failed"; }
}

build_baseline() {
    local reuse=0
    # A linked worktree has a .git file, not a directory.
    if [ -e "$baseline_src/.git" ] &&
       [ "$(git -C "$baseline_src" rev-parse HEAD 2>/dev/null || true)" = "$baseline_commit" ]; then
        reuse=1
    fi
    if [ "$reuse" -eq 0 ]; then
        if git -C "$repo_root" worktree list --porcelain | grep -Fqx "worktree $baseline_src"; then
            git -C "$repo_root" worktree remove --force "$baseline_src" >/dev/null
        elif [ -e "$baseline_src" ]; then
            die "$baseline_src exists and is not a registered worktree"
        fi
        git -C "$repo_root" worktree add --detach "$baseline_src" "$baseline_commit" >/dev/null
    fi
    [ -d "$repo_root/third_party/lz4" ] || die "third_party/lz4 is missing; run a normal build first"
    mkdir -p "$baseline_src/third_party"
    if [ ! -e "$baseline_src/third_party/lz4" ] && [ ! -L "$baseline_src/third_party/lz4" ]; then
        ln -s "$repo_root/third_party/lz4" "$baseline_src/third_party/lz4"
    fi
    cmake -S "$baseline_src" -B "$baseline_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TEST=OFF -DBUILD_CLI=ON \
        >"$tmp_root/configure-baseline.log" 2>&1 \
        || { cat "$tmp_root/configure-baseline.log" >&2; die "baseline configure failed"; }
    cmake --build "$baseline_build" >"$tmp_root/build-baseline.log" 2>&1 \
        || { cat "$tmp_root/build-baseline.log" >&2; die "baseline build failed"; }
}

pack_command() {
    printf '%s pack -f -t %s -c lz4 %s %s' \
        "$(quote "$1")" "$threads" "$(quote "$dataset")" "$(quote "$2")"
}

unpack_command() {
    printf '%s unpack -t %s %s %s' \
        "$(quote "$1")" "$threads" "$(quote "$2")" "$(quote "$3")"
}

run_benchmark() {
    local json=$1
    local prepare=$2
    local base_cmd=$3
    local branch_cmd=$4
    if [ -n "$prepare" ]; then
        hyperfine --warmup 1 --runs "$runs" --export-json "$json" --prepare "$prepare" \
            -n baseline "$base_cmd" -n branch "$branch_cmd" >/dev/null
    else
        hyperfine --warmup 1 --runs "$runs" --export-json "$json" \
            -n baseline "$base_cmd" -n branch "$branch_cmd" >/dev/null
    fi
}

compare() {
    local json=$1
    local label=$2
    local base_mean branch_mean base_mem branch_mem
    base_mean=$(jq -r '.results[0].summary.time_wall_clock.mean' "$json")
    branch_mean=$(jq -r '.results[1].summary.time_wall_clock.mean' "$json")
    base_mem=$(jq -r '.results[0].summary.memory_peak_resident.mean' "$json")
    branch_mem=$(jq -r '.results[1].summary.memory_peak_resident.mean' "$json")
    awk -v b="$base_mean" -v c="$branch_mean" -v bm="$base_mem" -v cm="$branch_mem" \
        -v label="$label" -v lim="$max_regression" '
        function pct(new, old) { return old > 0 ? (new - old) / old * 100 : 0 }
        BEGIN {
            dt = pct(c, b)
            dm = pct(cm, bm)
            printf "%-7s time %7.3fs -> %7.3fs (%+6.1f%%)   peak RSS %8.1f MiB -> %8.1f MiB (%+6.1f%%)\n",
                   label, b, c, dt, bm / 1048576, cm / 1048576, dm
            if (dt > lim || dm > lim) exit 2
        }'
}

printf 'dataset:  %s\n' "$dataset"
printf 'baseline: %s (%s)\n' "$baseline_commit" "$ref"
printf 'current:  %s\n' "$current_commit"
printf 'runs:     %s, threads: %s, mode: %s\n\n' "$runs" "$threads" "$mode"

build_current
build_baseline
[ -x "$current_bin" ] || die "current binary was not built: $current_bin"
[ -x "$baseline_bin" ] || die "baseline binary was not built: $baseline_bin"

failed=0
archive="$tmp_root/base.ctar"
out_base="$tmp_root/out-baseline"
out_branch="$tmp_root/out-current"

if [ "$mode" = pack ] || [ "$mode" = both ]; then
    # Flush the previous run's writes first: hyperfine runs all baseline samples
    # before the branch samples, and a dirty-page backlog would penalise whichever
    # command runs second.
    run_benchmark "$tmp_root/pack.json" "sync" \
        "$(pack_command "$baseline_bin" "$archive")" \
        "$(pack_command "$current_bin" "$tmp_root/current.ctar")"
    compare "$tmp_root/pack.json" pack || failed=1
fi

if [ "$mode" = unpack ] || [ "$mode" = both ]; then
    # Both binaries unpack the same archive; only the unpack path differs.
    "$baseline_bin" pack -f -t "$threads" -c lz4 "$dataset" "$archive" >/dev/null \
        || die "failed to create the unpack fixture archive"
    prepare="rm -rf $(quote "$out_base") $(quote "$out_branch") && sync"
    run_benchmark "$tmp_root/unpack.json" "$prepare" \
        "$(unpack_command "$baseline_bin" "$archive" "$out_base")" \
        "$(unpack_command "$current_bin" "$archive" "$out_branch")"
    compare "$tmp_root/unpack.json" unpack || failed=1
fi

rm -rf "$out_base" "$out_branch" "$tmp_root/current.ctar"

if [ "$clean" -eq 1 ]; then
    if git -C "$repo_root" worktree list --porcelain | grep -Fqx "worktree $baseline_src"; then
        git -C "$repo_root" worktree remove --force "$baseline_src" >/dev/null
    fi
    rm -rf "$tmp_root"
fi

if [ "$failed" -ne 0 ]; then
    printf '\nperformance gate: regression beyond %s%%\n' "$max_regression" >&2
    exit 1
fi
printf '\nperformance gate: ok (threshold %s%%)\n' "$max_regression"
