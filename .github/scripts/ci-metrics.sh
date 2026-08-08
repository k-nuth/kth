#!/usr/bin/env bash
# Records what a CI job spent its time on, and renders it into the job summary.
#
# Why a script and not inline shell: the point of this is comparing a slow run
# against a normal one, and two summaries can only be compared if they were
# produced by the same code. Three workflows calling one script keeps the shape
# identical; three copies of inline shell would drift within a month.
#
# Nothing here may change what the job DOES. It measures and reports.
#
# Subcommands
#   phase-start <name>            note when a phase began
#   phase-end   <name> [rc]       note how long it took, and how it ended
#   ccache-snapshot <before|after>
#   note <key> <value>            record one fact for the summary
#   summary                       render everything into $GITHUB_STEP_SUMMARY
#
# State lives in a directory rather than in $GITHUB_ENV, because a phase that
# fails still has to be reportable and $GITHUB_ENV is only read between steps.

set -uo pipefail

STATE_DIR="${CI_METRICS_DIR:-${RUNNER_TEMP:-/tmp}/ci-metrics}"
mkdir -p "${STATE_DIR}"

now_seconds() {
    date +%s
}

# ---------------------------------------------------------------------------
# Timing
# ---------------------------------------------------------------------------

phase_start() {
    local name="$1"
    now_seconds > "${STATE_DIR}/${name}.start"
}

phase_end() {
    local name="$1"
    local rc="${2:-0}"
    local start end

    if [ ! -f "${STATE_DIR}/${name}.start" ]; then
        # Said, not guessed. A phase whose start was never recorded has no
        # duration, and printing one would invent it.
        echo "unstarted" > "${STATE_DIR}/${name}.duration"
        echo "${rc}" > "${STATE_DIR}/${name}.rc"
        return 0
    fi

    start=$(cat "${STATE_DIR}/${name}.start")
    end=$(now_seconds)
    echo $(( end - start )) > "${STATE_DIR}/${name}.duration"
    echo "${rc}" > "${STATE_DIR}/${name}.rc"
}

note() {
    local key="$1"
    shift
    printf '%s\n' "$*" > "${STATE_DIR}/note.${key}"
}

# ---------------------------------------------------------------------------
# ccache
# ---------------------------------------------------------------------------

# Absence is an answer and so is an unreadable stat file. Neither is zero: a
# summary reporting "0 hits" for a run where ccache was never installed would
# send someone hunting for a cache problem that does not exist, and one
# reporting nothing at all would let a silently missing ccache look normal.
ccache_snapshot() {
    local label="$1"
    local out="${STATE_DIR}/ccache.${label}"

    if ! command -v ccache >/dev/null 2>&1; then
        printf 'status\tabsent\n' > "${out}"
        return 0
    fi

    local version
    version=$(ccache --version 2>/dev/null | head -n 1)
    printf 'version\t%s\n' "${version:-unknown}" > "${out}"

    # --print-stats is the machine-readable form (ccache >= 4.4). Parsing the
    # human table instead would break on every release that reflows it.
    local machine
    if machine=$(ccache --print-stats 2>/dev/null) && [ -n "${machine}" ]; then
        printf 'status\tok\n' >> "${out}"
        printf '%s\n' "${machine}" >> "${out}"
        return 0
    fi

    # Older ccache: keep the raw text so a human still has something, and say
    # the numbers could not be read rather than showing zeros for them.
    local human
    if human=$(ccache --show-stats 2>/dev/null) && [ -n "${human}" ]; then
        printf 'status\tunparsed\n' >> "${out}"
        printf '%s\n' "${human}" | sed 's/^/raw\t/' >> "${out}"
        return 0
    fi

    printf 'status\tunreadable\n' >> "${out}"
}

ccache_field() {
    local label="$1" key="$2"
    local file="${STATE_DIR}/ccache.${label}"
    [ -f "${file}" ] || { printf 'n/a'; return; }
    local value
    value=$(awk -F'\t' -v k="${key}" '$1 == k { print $2; exit }' "${file}")
    printf '%s' "${value:-n/a}"
}

ccache_status() {
    ccache_field "$1" status
}

# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

# A phase is in exactly one of four states, and they are not interchangeable.
# "Not reached" and "measurement unavailable" both have no duration, and a
# reader chasing a slow run needs to know which: the first says an earlier phase
# failed, the second says this phase started and the job died inside it.
#
# A missing timing file is never a duration of zero. Zero is a measurement, and
# there is none.
phase_state() {
    local name="$1"

    if [ ! -f "${STATE_DIR}/${name}.start" ] && [ ! -f "${STATE_DIR}/${name}.duration" ]; then
        printf 'not-reached'
        return
    fi

    if [ ! -f "${STATE_DIR}/${name}.duration" ] || [ ! -f "${STATE_DIR}/${name}.rc" ]; then
        # Started and never closed: the step was killed, timed out, or the
        # runner went away mid-phase.
        printf 'unavailable'
        return
    fi

    if [ "$(cat "${STATE_DIR}/${name}.duration")" = "unstarted" ]; then
        # Closed without ever being opened — the instrumentation was wired
        # wrong, and saying so beats printing a number derived from nothing.
        printf 'unavailable'
        return
    fi

    if [ "$(cat "${STATE_DIR}/${name}.rc")" = "0" ]; then
        printf 'ok'
    else
        printf 'failed'
    fi
}

duration_of() {
    local name="$1"
    case "$(phase_state "${name}")" in
        not-reached) printf '—' ;;
        unavailable) printf '—' ;;
        *)
            local value
            value=$(cat "${STATE_DIR}/${name}.duration")
            printf '%dm %02ds' $(( value / 60 )) $(( value % 60 ))
            ;;
    esac
}

outcome_of() {
    local name="$1"
    case "$(phase_state "${name}")" in
        not-reached) printf 'not reached (an earlier phase failed)' ;;
        unavailable) printf 'measurement unavailable' ;;
        ok)          printf 'ok' ;;
        failed)      printf 'failed (exit %s)' "$(cat "${STATE_DIR}/${name}.rc")" ;;
    esac
}

note_of() {
    local key="$1"
    local file="${STATE_DIR}/note.${key}"
    if [ -f "${file}" ]; then cat "${file}"; else printf 'n/a'; fi
}

emit() {
    printf '%s\n' "$*" >> "${GITHUB_STEP_SUMMARY:-/dev/stdout}"
}

ccache_table() {
    local before after
    before=$(ccache_status before)
    after=$(ccache_status after)

    emit "#### ccache"
    emit ""

    if [ "${before}" = "absent" ] || [ "${after}" = "absent" ]; then
        emit "**ccache is not installed on this runner.** Every compile ran cold."
        emit ""
        emit "This is a statement about the runner, not a measurement of zero hits."
        return
    fi

    if [ "${after}" = "unreadable" ]; then
        emit "**ccache is installed but its statistics could not be read.**"
        emit ""
        emit "Version: \`$(ccache_field after version)\`. The numbers below are"
        emit "unavailable — not zero. A cache-hit problem cannot be ruled in or out"
        emit "from this run."
        return
    fi

    if [ "${after}" = "unparsed" ]; then
        emit "**This ccache is too old for machine-readable statistics**"
        emit "(\`$(ccache_field after version)\`), so the fields below could not be"
        emit "extracted. The raw output is in the job log."
        return
    fi

    # Both lines come from the workflow, because the answer differs by
    # workflow: where the save is a post-job action its outcome is not knowable
    # here and the note says so, and where it is an explicit prior step the note
    # carries its real result. A sentence written here would have to be true of
    # every caller, and no such sentence exists.
    emit "Restore: **$(note_of ccache_restore)**"
    emit ""
    emit "Save: **$(note_of ccache_save)**"
    emit ""
    emit "| | Before build | After build |"
    emit "|---|---:|---:|"
    emit "| Direct hits | $(ccache_field before direct_cache_hit) | $(ccache_field after direct_cache_hit) |"
    emit "| Preprocessed hits | $(ccache_field before preprocessed_cache_hit) | $(ccache_field after preprocessed_cache_hit) |"
    emit "| Misses | $(ccache_field before cache_miss) | $(ccache_field after cache_miss) |"
    emit "| Uncacheable calls | $(ccache_field before uncacheable) | $(ccache_field after uncacheable) |"
    emit "| Cache size (KiB) | $(ccache_field before cache_size_kibibyte) | $(ccache_field after cache_size_kibibyte) |"
    emit "| Cleanups (evictions) | $(ccache_field before cleanups_performed) | $(ccache_field after cleanups_performed) |"
    emit ""
    emit "Counters are zeroed between the two snapshots, so the *after* column is"
    emit "this run's work and the *before* column is what the restored cache"
    emit "already held. Size and cleanups are cumulative in both."
    emit ""
    emit "ccache: \`$(ccache_field after version)\`"
}

summary() {
    emit ""
    emit "### Where the time went"
    emit ""
    emit "| Phase | Duration | Outcome |"
    emit "|---|---:|---|"
    emit "| Dependency resolution | $(duration_of deps) | $(outcome_of deps) |"
    emit "| CMake configuration | $(duration_of configure) | $(outcome_of configure) |"
    emit "| Compilation | $(duration_of compile) | $(outcome_of compile) |"
    emit "| Tests | $(duration_of tests) | $(outcome_of tests) |"

    # Coverage-only phases. Shown when the job recorded them, absent otherwise,
    # so one renderer serves every workflow without inventing rows a build job
    # never had.
    if [ -f "${STATE_DIR}/coverage_generate.start" ] || \
       [ -f "${STATE_DIR}/coverage_generate.duration" ]; then
        emit "| Coverage generation | $(duration_of coverage_generate) | $(outcome_of coverage_generate) |"
        emit "| Tracefile validation | $(duration_of coverage_validate) | $(outcome_of coverage_validate) |"
        emit "| Upload | $(duration_of upload) | $(outcome_of upload) |"
    fi
    emit ""
    emit "A phase with no duration is never shown as zero. **Not reached** means an"
    emit "earlier phase failed and this one never began; **measurement unavailable**"
    emit "means it began and the job ended inside it."
    emit ""
    emit "#### Toolchain"
    emit ""
    emit "| | |"
    emit "|---|---|"
    emit "| \`CMAKE_CXX_COMPILER\` | \`$(note_of cmake_cxx_compiler)\` |"
    emit "| \`CMAKE_CXX_COMPILER_LAUNCHER\` | \`$(note_of cmake_cxx_launcher)\` |"
    emit "| Compiler \`--version\` | \`$(note_of compiler_version)\` |"
    emit ""
    emit "The launcher is not the compiler. A ccache in front of clang is still"
    emit "clang doing the compiling, and reading the launcher's version as the"
    emit "toolchain's is how a compiler change goes unnoticed."
    emit ""
    emit "#### Parallelism"
    emit ""
    emit "| | |"
    emit "|---|---|"
    emit "| CPUs visible to the runner | \`$(note_of cpus_visible)\` |"
    emit "| \`CMAKE_BUILD_PARALLEL_LEVEL\` | \`$(note_of cmake_parallel_level)\` |"
    emit "| \`--parallel\` argument | \`$(note_of parallel_argument)\` |"
    emit "| Generator / build tool | \`$(note_of build_tool)\` |"
    emit ""
    emit "These are what was **asked for**. How many compiler processes actually"
    emit "ran at once is not observed here, so nothing above should be read as a"
    emit "measurement of achieved concurrency."
    emit ""
    ccache_table
}

# ---------------------------------------------------------------------------

case "${1:-}" in
    phase-start)     phase_start "$2" ;;
    phase-end)       phase_end "$2" "${3:-0}" ;;
    ccache-snapshot) ccache_snapshot "$2" ;;
    note)            key="$2"; shift 2; note "${key}" "$@" ;;
    summary)         summary ;;
    *)
        echo "usage: $0 {phase-start|phase-end|ccache-snapshot|note|summary} ..." >&2
        exit 2
        ;;
esac
