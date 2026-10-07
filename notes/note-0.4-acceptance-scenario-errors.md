# 0.4 acceptance scenarios: two errors in goal.md, left as written

The 0.4 develop ("Standard streams, pipes, exit codes and the hsh shell",
develop PR #18: https://github.com/paul-neata/haisos/pull/18) has two errors in
the acceptance scenarios of `develop-plan/goal.md`. Each one expects output
that no shell would print. hsh behaves as dash and bash do. By the user's
decision (2026-10-07), `goal.md` keeps both errors as written. The end-to-end
test `tests/haisos/hsh.haisostest/hsh.haisostest.js`, added in hsh--interactive
(PR #40: https://github.com/paul-neata/haisos/pull/40), uses corrected inputs,
so it differs from the scenarios on purpose.

## Scenario 3: `hello` does not match `hi*`

The scenario runs `RUN /bin/hsh /count.sh hello`, whose script ends with
`case "$1" in hi*) echo "greeted";; *) echo "plain";; esac`, and expects
`greeted`. `hi*` matches only words that start with `hi`, and `hello` starts
with `he`, so bash, dash and hsh all print `plain`. Checked with bash:

```
$ bash -c 'case "$1" in hi*) echo greeted;; *) echo plain;; esac' _ hello
plain
$ bash -c 'case "$1" in hi*) echo greeted;; *) echo plain;; esac' _ hi
greeted
```

The PR #39 review found this (hsh--control-flow,
https://github.com/paul-neata/haisos/pull/39). The end-to-end test runs
`/count.sh hi` and expects `greeted`. To fix the scenario, run it with `hi`,
or keep `hello` and expect `plain`.

## The preamble: `/abc.txt` has no final newline

The preamble creates the file with:

```
CREATE /abc.txt multiline END
one
two
three
END
```

A haisosfile `multiline` block drops the newline just before its end marker,
so the file is `one\ntwo\nthree`, with no newline after `three` (see "The
`haisosfile` DSL" in the root `CLAUDE.md`). GNU `wc` counts newline characters,
so bash with GNU `wc` gives `2` lines and `13` bytes. Scenario 1 expects
`cat /abc.txt | wc -l` to print `3`, and scenario 2 expects `14 /out.txt`.
Checked with bash:

```
$ printf 'one\ntwo\nthree'   > a.txt; wc -l < a.txt; cat < a.txt >> o1; wc -c o1
2
13 o1
$ printf 'one\ntwo\nthree\n' > b.txt; wc -l < b.txt; cat < b.txt >> o2; wc -c o2
3
14 o2
```

The whole-develop review found this (`develop-plan/final-review.md`, on
develop PR #18). The end-to-end test adds an empty line before `END`, which
gives the file its final newline, and expects `3` and `14 /out.txt`. To fix
the scenarios, add that empty line to the preamble, or expect `2` and
`13 /out.txt`.
