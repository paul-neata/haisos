# The task container: the base image (./Dockerfile.base, the tools) plus what
# changes often -- the user, the entrypoint and the prompt template. Built in
# seconds by scripts/develop/image.sh; run by scripts/develop/container.sh and
# scripts/develop/claude_docker.sh. See .claude/develop/WORKFLOW.md.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

# One unprivileged user with the host user's ids, so the files it leaves in the
# mounted workspace belong to the host user (ubuntu:24.04 ships "ubuntu" as 1000).
ARG UID=1000
ARG GID=1000
RUN (userdel -r ubuntu 2>/dev/null || true) && (groupdel ubuntu 2>/dev/null || true) \
 && groupadd -g "${GID}" dev \
 && useradd -m -u "${UID}" -g "${GID}" -s /bin/bash dev \
 && mkdir -p /work /gitdir /in /out && chown dev:dev /work /gitdir /out

COPY entrypoint.sh task_prompt.md /opt/haisos-develop/
RUN chmod 0755 /opt/haisos-develop/entrypoint.sh \
 && chmod 0644 /opt/haisos-develop/task_prompt.md

USER dev
WORKDIR /work
ENTRYPOINT ["/opt/haisos-develop/entrypoint.sh"]
