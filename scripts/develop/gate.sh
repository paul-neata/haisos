#!/bin/bash
# The security gate: checks what a task branch changes before anything of it is
# pushed, and again before it is reviewed (see .claude/develop/WORKFLOW.md,
# "Security"). It reads git objects only -- nothing from the branch is checked
# out or run.
#
# Usage: scripts/develop/gate.sh <base-rev> <head-rev> [--develop] [--out <file>]
#   base-rev   where the task started (usually the merge base with origin/develop)
#   head-rev   the branch head to check
#   --develop  checking the whole develop against master: develop-plan/, notes/
#              and HAISOS_VERSION are then expected changes, not blocked
#
# Prints a report: a first line "GATE: PASS | REVIEW | BLOCK", then one line per
# finding, "BLOCK <where> -- <why>" or "REVIEW <where> -- <why>: <text>".
# Exit status: 0 PASS, 1 REVIEW (may be pushed; the reviewer must look at every
# item), 2 BLOCK (must not be pushed), 3 error.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

BASE="${1:-}"; HEAD="${2:-}"
[ -n "$BASE" ] && [ -n "$HEAD" ] || { echo "usage: gate.sh <base-rev> <head-rev> [--develop] [--out <file>]" >&2; exit 3; }
shift 2
OUTFILE="" DEVELOP_MODE=0
while [ $# -gt 0 ]; do
    case "$1" in
        --develop) DEVELOP_MODE=1; shift;;
        --out) OUTFILE="${2:-}"; shift 2;;
        *) echo "gate: unknown option '$1'" >&2; exit 3;;
    esac
done

G=(git -C "$DEV_REPO" -c core.quotePath=false)
BASE_SHA=$("${G[@]}" rev-parse --verify -q "$BASE^{commit}") || { echo "gate: no commit '$BASE'" >&2; exit 3; }
HEAD_SHA=$("${G[@]}" rev-parse --verify -q "$HEAD^{commit}") || { echo "gate: no commit '$HEAD'" >&2; exit 3; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
: > "$TMP/block"; : > "$TMP/review"
block()  { printf 'BLOCK %s -- %s\n' "$1" "$2" >> "$TMP/block"; }
review() { printf 'REVIEW %s -- %s\n' "$1" "$2" >> "$TMP/review"; }

# --- History ---------------------------------------------------------------
if ! "${G[@]}" merge-base --is-ancestor "$BASE_SHA" "$HEAD_SHA"; then
    block "history" "the branch does not descend from ${BASE_SHA:0:12} (rewritten history or wrong base)"
fi
COMMITS=$("${G[@]}" rev-list --count "$BASE_SHA..$HEAD_SHA")
[ "$COMMITS" -le 60 ] || review "history" "$COMMITS commits"
if [ -n "$("${G[@]}" rev-list --min-parents=2 "$BASE_SHA..$HEAD_SHA")" ]; then
    review "history" "merge commits inside the task branch"
fi
if "${G[@]}" log --format=%B "$BASE_SHA..$HEAD_SHA" | grep -qiE '^(co-authored-by|signed-off-by):'; then
    review "history" "commit messages carry trailers (dropped by the squash merge)"
fi

# --- Paths and modes -------------------------------------------------------
PLAN_PATHS='^(develop-plan|notes)/|^HAISOS_VERSION$'
BLOCK_PATHS='^(scripts|\.github|extern)/|(^|/)\.claude(/|$)|(^|/)(\.gitattributes|\.gitmodules|\.envrc|\.mcp\.json|CLAUDE\.local\.md|\.pre-commit-config\.yaml)$|(^|/)(\.vscode|\.devcontainer|\.idea|\.husky)/'
SCRIPT_FILES='\.(sh|bash|zsh|bat|cmd|ps1|psm1|py|js|mjs|cjs|ts|pl|rb|lua)$'
CODE_FILES='\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl|ipp)$'
CMAKE_FILES='(^|/)CMakeLists\.txt$|\.cmake$'

while IFS= read -r -d '' meta && IFS= read -r -d '' path; do
    read -r old_mode new_mode _ new_sha status <<< "${meta#:}"
    if [[ "$path" =~ [[:cntrl:]] ]]; then
        block "$(printf '%q' "$path")" "control characters in a path"; continue
    fi
    [[ "$path" =~ $BLOCK_PATHS ]] && block "$path" "protected path ($status)"
    if [[ "$path" =~ $PLAN_PATHS ]]; then
        if [ "$DEVELOP_MODE" = 0 ]; then block "$path" "protected path ($status)"; else continue; fi
    fi
    case "$new_mode" in
        120000) block "$path" "symbolic link";;
        160000) block "$path" "git submodule";;
        100755) [ "$old_mode" = 100755 ] || review "$path" "executable file";;
    esac
    if [ "$status" != D ] && [ "$new_mode" != 160000 ]; then
        size=$("${G[@]}" cat-file -s "$new_sha" 2>/dev/null || echo 0)
        [ "$size" -le 1048576 ] || block "$path" "file over 1 MB ($size bytes)"
    fi
    [[ "$path" =~ (^|/)(CLAUDE|AGENTS)\.md$ ]] && review "$path" "agent instructions changed: read every changed line for text aimed at AI agents"
    [[ "$path" =~ (^|/)\.gitignore$ ]] && review "$path" ".gitignore changed: check nothing is being hidden"
    [[ "$path" =~ $CMAKE_FILES ]] && [ "$status" != D ] && review "$path" "build file changed: it runs at configure/build time, in CI and later on the host"
    [[ "$path" =~ (^|/)(CMakePresets\.json|CMakeUserPresets\.json|Makefile|GNUmakefile|[^/]*\.mk|meson\.build|Dockerfile[^/]*)$ ]] \
        && [ "$status" != D ] && review "$path" "build definition changed: it can run commands at build time"
    [[ "$path" =~ (^|/)package(-lock)?\.json$ ]] && [ "$status" != D ] \
        && review "$path" "npm package file changed: its lifecycle scripts run on npm install"
    [[ "$path" =~ $SCRIPT_FILES ]] && [ "$status" != D ] && review "$path" "script file changed: it runs in CI and later on the host"
    [[ "$path" =~ ^tests/tool/llm_cache_proxy_database/ ]] && review "$path" "recorded LLM traffic changed"
done < <("${G[@]}" diff --raw -z --no-renames --no-abbrev "$BASE_SHA" "$HEAD_SHA")

while IFS=$'\t' read -r -d '' added _deleted path; do
    [ "$added" = "-" ] && review "$path" "binary file"
done < <("${G[@]}" diff --numstat -z --no-renames "$BASE_SHA" "$HEAD_SHA")

# --- Added lines -----------------------------------------------------------
# One line per added line: path <TAB> line number <TAB> text.
"${G[@]}" diff -U0 --no-color --no-ext-diff --no-textconv --no-renames "$BASE_SHA" "$HEAD_SHA" | awk '
    /^\+\+\+ / { f = substr($0, 5); sub(/\t$/, "", f); sub(/^b\//, "", f); next }
    /^--- /    { next }
    /^@@ /     { match($0, /\+[0-9]+/); ln = substr($0, RSTART + 1, RLENGTH - 1) + 0; next }
    /^\+/      { print f "\t" ln "\t" substr($0, 2); ln++ }
' > "$TMP/added.tsv"

# Credentials of this machine, verbatim.
local_secret_values > "$TMP/secrets" || true
if [ -s "$TMP/secrets" ] && grep -qFf "$TMP/secrets" "$TMP/added.tsv"; then
    grep -Ff "$TMP/secrets" "$TMP/added.tsv" | cut -f1,2 | tr '\t' ':' | while read -r where; do
        block "$where" "contains a credential of this machine"
    done
fi
rm -f "$TMP/secrets"

# scan <BLOCK|REVIEW> <path regex, or ''> <line regex> <why> [nocase]
PATTERNS=()
scan() { PATTERNS+=("$1"$'\x1f'"$2"$'\x1f'"$3"$'\x1f'"$4"$'\x1f'"${5:-}"); }

# Secrets
scan BLOCK '' 'gh[pousr]_[A-Za-z0-9]{36}|github_pat_[A-Za-z0-9_]{40,}' 'GitHub token'
scan BLOCK '' 'sk-ant-[A-Za-z0-9_-]{20,}' 'Anthropic key'
scan BLOCK '' 'AKIA[0-9A-Z]{16}' 'AWS access key'
scan BLOCK '' '-----BEGIN ([A-Z0-9]+ )*PRIVATE KEY-----' 'private key'
scan BLOCK '' 'xox[abprs]-[A-Za-z0-9-]{10,}|AIza[0-9A-Za-z_-]{35}' 'API token'
scan REVIEW '' '(api[_-]?key|secret|token|passw(or)?d)["'\'']?[[:space:]]*[:=][[:space:]]*["'\''][A-Za-z0-9/+_=.-]{16,}["'\'']' 'possible hard-coded secret' nocase
# Reaching out of the container, the repository or the process
scan REVIEW '' '\.ssh/|id_(rsa|dsa|ecdsa|ed25519)|authorized_keys|known_hosts|\.config/gh|hosts\.yml|git-credentials|credential\.helper|\.netrc|\.claude\.json|credentials\.json|\.anthropic|\.ollama/' 'credential locations' nocase
scan REVIEW '' 'ssh_auth_sock|github_token|gh_token|anthropic_(api_key|auth_token|base_url)|aws_(secret|access)_' 'credential variables' nocase
scan REVIEW '' '/mnt/[a-z]/|[a-z]:\\\\users|powershell|cmd\.exe|wsl\.exe|wslpath|docker\.sock|/var/run/docker|/proc/self/environ|/proc/[0-9]+/|/etc/(passwd|shadow|sudoers)|/dev/(tcp|udp)/' 'host paths / host escape' nocase
scan REVIEW '' '(^|[^[:alnum:]_.-])(curl|wget|nc|ncat|socat|scp|rsync|ssh|sudo|chmod|crontab)[[:space:]]+-' 'shell command'
scan REVIEW '' 'base64[[:space:]]+(-d|--decode)|xxd[[:space:]]+-r|(^|[^[:alnum:]_])eval[[:space:]]*[("$]' 'decode / eval'
scan REVIEW '' '[A-Za-z0-9+/]{160,}={0,2}|(\\x[0-9a-fA-F]{2}){16,}' 'long encoded blob'
# Text aimed at the AI reviewer or at agents
scan REVIEW '' '(ignore|disregard|forget)[[:space:]]+(all[[:space:]]+|any[[:space:]]+|the[[:space:]]+)?(previous|prior|above|earlier|other)[[:space:]]+(instructions|rules|prompts?)|(note|message|instructions?)[[:space:]]+(to|for)[[:space:]]+(the[[:space:]]+)?(ai|llm|claude|reviewer|assistant|agent)s?([^[:alnum:]]|$)|approve[[:space:]]+(this|the)[[:space:]]+(pr|change|pull)|do[[:space:]]+not[[:space:]]+(flag|report|mention)|<\|?(system|im_start)|\[/?INST\]' 'text aimed at AI reviewers or agents (prompt injection?)' nocase
# C++: processes, environment, network, the filesystem outside the OS's root
scan REVIEW "$CODE_FILES" '(^|[^[:alnum:]_])(system|popen|_popen|_wpopen|execl|execlp|execle|execv|execve|execvp|execvpe|fork|vfork|posix_spawnp?|CreateProcess[AW]?|ShellExecute(Ex)?[AW]?|WinExec|_spawn[a-z]*)[[:space:]]*\(' 'starts a process'
scan REVIEW "$CODE_FILES" 'getenv[[:space:]]*\(|_wgetenv|_dupenv_s|GetEnvironmentVariable|secure_getenv|environ\[' 'reads the environment'
scan REVIEW "$CODE_FILES" '(^|[^[:alnum:]_])(socket|connect|getaddrinfo|gethostbyname)[[:space:]]*\(|WinHttp(Open|Connect)|curl_easy_init|emscripten_fetch|https?://' 'network access'
scan REVIEW "$CODE_FILES" '(^|[^[:alnum:]_])(getpwuid|SHGetFolderPath|SHGetKnownFolderPath)|USERPROFILE|APPDATA|"/home/|"/root/|"~/' 'home directory'
# Build files
scan REVIEW "$CMAKE_FILES" 'execute_process|add_custom_command|add_custom_target|file[[:space:]]*\([[:space:]]*(DOWNLOAD|UPLOAD|CREATE_LINK)|ExternalProject|FetchContent|_LAUNCHER|install[[:space:]]*\([[:space:]]*(CODE|SCRIPT)|try_run|\$ENV\{|include[[:space:]]*\(' 'build-time code' nocase

while IFS=$'\t' read -r path line text; do
    for p in "${PATTERNS[@]}"; do
        IFS=$'\x1f' read -r level pre re why nocase <<< "$p"
        [ -z "$pre" ] || [[ "$path" =~ $pre ]] || continue
        if [ -n "$nocase" ]; then shopt -s nocasematch; fi
        if [[ "$text" =~ $re ]]; then
            snippet=$(printf '%s' "$text" | tr -d '\r' | sed 's/^[[:space:]]*//' | cut -c1-140)
            if [ "$level" = BLOCK ]; then block "$path:$line" "$why"
            else review "$path:$line" "$why: $snippet"; fi
        fi
        shopt -u nocasematch
    done
done < "$TMP/added.tsv"

# --- Report ----------------------------------------------------------------
NB=$(wc -l < "$TMP/block"); NR=$(wc -l < "$TMP/review")
if [ "$NB" -gt 0 ]; then VERDICT=BLOCK; RC=2
elif [ "$NR" -gt 0 ]; then VERDICT=REVIEW; RC=1
else VERDICT=PASS; RC=0; fi
{
    echo "GATE: $VERDICT ($NB blocking, $NR to review; ${BASE_SHA:0:12}..${HEAD_SHA:0:12}, $COMMITS commits)"
    sort -u "$TMP/block"
    awk '!seen[$0]++' "$TMP/review" | take 300
    [ "$NR" -le 300 ] || echo "... $((NR - 300)) more REVIEW lines"
} > "$TMP/report"
[ -z "$OUTFILE" ] || cp "$TMP/report" "$OUTFILE"
cat "$TMP/report"
exit "$RC"
