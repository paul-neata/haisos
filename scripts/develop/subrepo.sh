#!/bin/bash
# The containers' workspace: the repository's subrepo/ folder and its git
# metadata in $HAISOS_DEVELOP_HOME/subrepos/<key>/ (see lib.sh).
#
# Usage:
#   scripts/develop/subrepo.sh where   print both paths
#   scripts/develop/subrepo.sh wipe    delete both (refused while a container
#                                      runs on them); the next run checks out afresh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

case "${1:-}" in
    where)
        echo "SUBREPO: $DEV_SUBREPO"
        echo "GIT: $DEV_SUBREPO_GIT";;
    wipe)
        lock_subrepo
        rm -rf "$DEV_SUBREPO" "$DEV_SUBREPO_GIT" "$DEV_SUBREPO_HOME/in" "$DEV_SUBREPO_HOME/out"
        echo "WIPED: $DEV_SUBREPO and $DEV_SUBREPO_GIT";;
    *)
        die "usage: subrepo.sh where|wipe";;
esac
