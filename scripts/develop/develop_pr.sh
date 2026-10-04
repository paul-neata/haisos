#!/bin/bash
# The develop PR -- develop into master. /develop-implement opens it when it
# starts; every /develop-update re-renders it from develop-plan/, so it is a
# live view of the develop on GitHub and keeps the implement session's memory
# and log (see .claude/develop/WORKFLOW.md, "The develop PR").
#
# Usage:
#   develop_pr.sh ensure    open it when there is none; prints "#<n> <url>"
#   develop_pr.sh update    re-render its title, description and draft state
#   develop_pr.sh summary   print the description without memory and log, and
#                           without links: the squash commit message of /develop-close
#   develop_pr.sh number    print its number, or nothing
# Title: "WIP [M.m] <title>", a draft, until the playbook's Phase is done;
# then "[M.m] <title>", ready. M.m is from HAISOS_VERSION, the title from
# develop-plan/goal.md. Rendered from this clone's files, which /develop-update
# has just synced; without develop-plan/ nothing is rendered.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

CMD="${1:-}"
pr_number() {
    gh pr list --base master --head develop --state open --json number -q '.[0].number // empty' 2>/dev/null || true
}
if [ "$CMD" = number ]; then pr_number; exit 0; fi
case "$CMD" in ensure|update|summary) ;; *) die "usage: develop_pr.sh ensure|update|summary|number";; esac

G="$DEV_PLAN/goal.md"; P="$DEV_PLAN/playbook.md"
if [ ! -f "$G" ] || [ ! -f "$P" ]; then
    echo "develop PR: no develop-plan/ in this clone, nothing rendered (the description is kept)"
    exit 0
fi

VERSION=$(tr -d '[:space:]' < "$DEV_REPO/HAISOS_VERSION")
MM=$(cut -d. -f1,2 <<< "$VERSION")
TITLE=$(sed -n 's/^# Develop: *//p' "$G" | take 1)
TITLE="${TITLE:-untitled}"
PHASE=$(sed -n 's/^Phase: *//p' "$P" | take 1)
PHASE="${PHASE:-planning}"
PR_TITLE="[$MM] $TITLE"
[ "$PHASE" = "done" ] || PR_TITLE="WIP $PR_TITLE"
SLUG=$(gh repo view --json nameWithOwner -q .nameWithOwner 2>/dev/null || true)
URL="https://github.com/$SLUG"

# The lines under "## <heading>" of a file, up to the next "## ".
section() {
    awk -v h="## $2" '$0 == h {f = 1; next} /^## / {f = 0} f' "$1" 2>/dev/null | sed '/./,$!d'
}
first_paragraph() { sed '/./,$!d' | awk 'NF == 0 {if (p) d = 1; next} !d {print; p = 1}'; }

render_head() {  # render_head full|summary
    local mode="$1" rows merged total done_n review_n blocked_n now line
    local num id status ver dep pr tries review notes title sha box
    rows=$(playbook_rows)
    merged=$(gh pr list --base develop --state merged --limit 500 --json headRefName,mergeCommit 2>/dev/null || echo '[]')
    total=$(grep -c . <<< "$rows" || true)
    done_n=$(awk -F'\037' '$3 == "done" || $3 == "obsolete"' <<< "$rows" | grep -c . || true)
    review_n=$(awk -F'\037' '$3 == "in-review"' <<< "$rows" | grep -c . || true)
    blocked_n=$(awk -F'\037' '$3 == "blocked"' <<< "$rows" | grep -c . || true)
    now=$(awk -F'\037' '$3 == "in-progress" || $3 == "in-review" {print $2 " (" $3 ")"}' <<< "$rows" | take 1)

    echo "Develop **$TITLE**, version \`$VERSION\`, phase: **$PHASE**."
    echo
    section "$G" Goal | first_paragraph
    echo
    echo "Progress: $done_n of $total tasks done, $review_n in review, $blocked_n blocked.${now:+ Now: $now.}"
    echo
    echo "**Tasks**"
    echo
    while IFS=$'\037' read -r num id status ver dep pr tries review notes; do
        [ -n "$id" ] || continue
        title=$(plan_field "$DEV_PLAN/tasks/$id.md" 'PR title' 2>/dev/null || true)
        box=" "
        case "$status" in done|obsolete) box=x;; esac
        line="- [$box] ${ver:+$ver }\`$id\`${title:+ -- $title}"
        case "$status" in
            done)
                sha=$(jq -r --arg b "task/$id" '[.[] | select(.headRefName == $b)] | last | .mergeCommit.oid // "" | .[0:7]' <<< "$merged" 2>/dev/null || true)
                line+=" -- ${pr:+$pr }merged${sha:+ as $sha}${review:+ -- review: $review}";;
            obsolete)
                line+=" -- obsolete${notes:+: $notes}";;
            todo)
                line+=" -- to do"
                [ "$mode" = summary ] || line+=" -- [plan]($URL/blob/develop/develop-plan/tasks/$id.md)";;
            blocked)
                line+=" -- **blocked**${notes:+: $notes}${pr:+ -- $pr}";;
            *)
                line+=" -- $status${pr:+ -- $pr}"
                [ "$mode" = summary ] || line+=" -- branch [task/$id]($URL/tree/task/$id)";;
        esac
        echo "$line"
    done <<< "$rows"

    local q d
    q=$(section "$P" Questions | grep -E '^- ' | grep -v -- '-> answered' || true)
    if [ -n "$q" ] && [ "$mode" = full ]; then
        echo; echo "**Open questions** (answered with /develop-plan)"; echo; echo "$q"
    fi
    d=$(section "$P" Directions | grep -E '^- ' | grep -v -- '-> done' || true)
    if [ -n "$d" ] && [ "$mode" = full ]; then
        echo; echo "**Directions to the implement session**"; echo; echo "$d"
    fi

    local fr="$DEV_PLAN/final-review.md" reviewed
    if [ -f "$fr" ]; then
        reviewed=$(sed -n 's/^- Reviewed: *//p' "$fr" | take 1)
        echo; echo "**Whole-develop review**${reviewed:+ ($reviewed)}"; echo
        section "$fr" Overview
        local tests; tests=$(section "$fr" "Final tests")
        [ -z "$tests" ] || { echo; echo "**Final tests**"; echo; echo "$tests"; }
    fi

    local tokens
    tokens=$(cat "$DEV_PLAN"/reviews/*.md 2>/dev/null \
        | sed -n 's/^- Tokens: claude \([0-9]*\), ollama input \([0-9]*\), ollama output \([0-9]*\).*/\1 \2 \3/p' \
        | awk '{c += $1; i += $2; o += $3; n++} END {if (n) printf "over %d reviewed task%s: Claude %d; Ollama %d input, %d output", n, (n == 1 ? "" : "s"), c, i, o}')
    [ -z "$tokens" ] || { echo; echo "**Tokens** $tokens."; }
}

render_full() {
    local head log budget total_log kept
    head=$(render_head full)
    local memory
    memory=$(sed '1{/^# /d}' "$DEV_PLAN/memory.md" 2>/dev/null | sed '/./,$!d')
    head+=$'\n\n**Memory** (the implement session'"'"'s notes to itself)'$'\n\n'"${memory:-none yet}"
    log=$(grep -E '^- ' "$DEV_PLAN/log.md" 2>/dev/null || true)
    budget=$(( 60000 - ${#head} - 400 ))
    total_log=$(grep -c . <<< "$log" || true)
    kept=$(awk -v b="$budget" '{a[NR] = $0} END {s = 0; for (i = NR; i >= 1; i--) {s += length(a[i]) + 1; if (s > b) break; k = i} for (i = k; i <= NR; i++) if (k) print a[i]}' <<< "$log")
    printf '%s\n\n**Log**\n\n' "$head"
    local shown; shown=$(grep -c . <<< "$kept" || true)
    [ "$shown" -ge "$total_log" ] || echo "($((total_log - shown)) earlier entries are in \`develop-plan/log.md\`'s history.)"
    [ -n "$kept" ] && echo "$kept" || echo "nothing yet"
    echo
    echo "_Rendered from \`develop-plan/\` at \`$(git -C "$DEV_REPO" rev-parse --short HEAD)\`, $(date -u '+%Y-%m-%d %H:%M UTC')._"
}

case "$CMD" in
    summary)
        render_head summary;;
    ensure|update)
        N=$(pr_number)
        BODY=$(mktemp)
        trap 'rm -f "$BODY"' EXIT
        render_full > "$BODY"
        WANT_DRAFT=true
        [ "$PHASE" != "done" ] || WANT_DRAFT=false
        if [ -z "$N" ]; then
            if [ "$CMD" = update ]; then echo "develop PR: none yet (/develop-implement opens it)"; exit 0; fi
            DRAFT=()
            [ "$WANT_DRAFT" = false ] || DRAFT=(--draft)
            PR_URL=$(gh pr create --base master --head develop "${DRAFT[@]}" --title "$PR_TITLE" --body-file "$BODY") \
                || die "gh pr create failed"
            echo "develop PR: #${PR_URL##*/} $PR_URL"
            exit 0
        fi
        gh pr edit "$N" --title "$PR_TITLE" --body-file "$BODY" > /dev/null || die "gh pr edit failed"
        IS_DRAFT=$(gh pr view "$N" --json isDraft -q .isDraft 2>/dev/null || echo "$WANT_DRAFT")
        if [ "$WANT_DRAFT" = true ] && [ "$IS_DRAFT" = false ]; then gh pr ready "$N" --undo > /dev/null 2>&1 || true; fi
        if [ "$WANT_DRAFT" = false ] && [ "$IS_DRAFT" = true ]; then gh pr ready "$N" > /dev/null 2>&1 || true; fi
        echo "develop PR: #$N \"$PR_TITLE\" ($([ "$WANT_DRAFT" = true ] && echo draft || echo ready for review)) $URL/pull/$N";;
esac
