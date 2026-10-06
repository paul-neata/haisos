#!/bin/bash
# Makes sure git in this clone reaches origin over SSH, without a prompt.
#
# A shell started by a tool (Claude Code's, a script's) may lack the
# SSH_AUTH_SOCK of the user's terminal, so ssh finds no agent, falls back to
# asking for the key's passphrase, and fails ("Permission denied (publickey)").
# Exporting the variable would last one shell only; so when origin cannot be
# reached as git is set up now, this looks for a running ssh agent holding a
# key, and if one lets git reach origin, writes it to this clone's own config:
#   git config --local core.sshCommand "ssh -o IdentityAgent=<socket>"
# Every later git command in the clone (and in its worktrees) then uses it,
# whatever shell it runs in. Run again, it checks and repairs that setting.
#
# Usage: scripts/develop/git_ssh.sh
# One line, "OK ...", "FIXED ..." or "FAIL ..."; exit status 1 on FAIL.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

GIT=(git -C "$DEV_REPO")
URL=$("${GIT[@]}" remote get-url origin 2>/dev/null) || die "no remote named origin"
if [[ ! "$URL" =~ ^(ssh://|[^/:]+@[^/:]+:) ]]; then
    echo "OK   origin is not reached over SSH ($URL)"
    exit 0
fi

# Whether git reaches origin with |ssh_command| as its ssh, never prompting.
reaches_origin() {
    timeout 30 "${GIT[@]}" -c core.sshCommand="$1 -o BatchMode=yes" ls-remote -q origin HEAD > /dev/null 2>&1
}

CURRENT=$("${GIT[@]}" config --local --get core.sshCommand 2>/dev/null) || CURRENT=""
if reaches_origin "${CURRENT:-ssh}"; then
    echo "OK   git reaches origin over SSH${CURRENT:+ (core.sshCommand: $CURRENT)}"
    exit 0
fi

# Agent sockets, most likely first: the shell's own, the fixed paths a
# ~/.bashrc or systemd usually gives one, then ssh-agent's default ones.
shopt -s nullglob
CANDIDATES=("${SSH_AUTH_SOCK:-}" "$HOME/.ssh/agent.sock" "$HOME/.ssh/ssh-agent.sock")
if [ -n "${XDG_RUNTIME_DIR:-}" ]; then
    CANDIDATES+=("$XDG_RUNTIME_DIR/ssh-agent.socket" "$XDG_RUNTIME_DIR/keyring/ssh" "$XDG_RUNTIME_DIR/gnupg/S.gpg-agent.ssh")
fi
CANDIDATES+=(/tmp/ssh-*/agent.*)

for socket in "${CANDIDATES[@]}"; do
    # The user's own socket, with an agent behind it holding at least one key.
    [ -n "$socket" ] && [ -S "$socket" ] && [ -O "$socket" ] || continue
    SSH_AUTH_SOCK="$socket" ssh-add -l > /dev/null 2>&1 || continue
    COMMAND="ssh -o IdentityAgent=$socket"
    if reaches_origin "$COMMAND"; then
        "${GIT[@]}" config --local core.sshCommand "$COMMAND"
        echo "FIXED git reaches origin through the ssh agent at $socket (set core.sshCommand in this clone)"
        exit 0
    fi
done

echo "FAIL git cannot reach origin over SSH ($URL), and no running ssh agent with a key lets it:" \
    "start one and add the key in a terminal (eval \"\$(ssh-agent -a ~/.ssh/agent.sock)\"; ssh-add)," \
    "or export its SSH_AUTH_SOCK in ~/.bashrc, then run this again"
exit 1
