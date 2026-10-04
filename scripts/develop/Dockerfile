# The task container: build tools, git, node, Claude Code and the ollama CLI --
# and no credentials of any kind. Built by scripts/develop/image.sh, which adds
# the host's own ollama binary to the build context; run by
# scripts/develop/container.sh. See .claude/develop/WORKFLOW.md.
FROM ubuntu:24.04

ARG UID=1000
ARG GID=1000
ARG CLAUDE_CODE_VERSION=latest

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        ca-certificates curl git jq cmake g++ make libcurl4-openssl-dev \
        nodejs npm python3 procps file less \
 && rm -rf /var/lib/apt/lists/*

RUN npm install -g "@anthropic-ai/claude-code@${CLAUDE_CODE_VERSION}" \
 && npm cache clean --force

# The ollama CLI, a client of the host's server (reached over --network host).
COPY ollama /usr/local/bin/ollama
COPY entrypoint.sh task_prompt.md /opt/haisos-develop/
RUN chmod 0755 /usr/local/bin/ollama /opt/haisos-develop/entrypoint.sh \
 && chmod 0644 /opt/haisos-develop/task_prompt.md

# One unprivileged user with the host user's ids, so the files it leaves in the
# mounted workspace belong to the host user (ubuntu:24.04 ships "ubuntu" as 1000).
RUN (userdel -r ubuntu 2>/dev/null || true) && (groupdel ubuntu 2>/dev/null || true) \
 && groupadd -g "${GID}" dev \
 && useradd -m -u "${UID}" -g "${GID}" -s /bin/bash dev \
 && mkdir -p /work /in /out && chown dev:dev /work /out

USER dev
WORKDIR /work
ENTRYPOINT ["/opt/haisos-develop/entrypoint.sh"]
