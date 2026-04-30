# the-copilot-is-gonna-ralph

A GitHub-Copilot-native, GitHub-Actions-driven feature pipeline. File an issue, slap a label on it, and Copilot ralphs through the four-stage workflow — PRD, design doc, implementation, merge — with you only signing off on PRs at each gate.

```text
issue + label `ralph:start`
        │
        ▼
[01-on-label]
   create branch feature/issue-{N} off main
   GraphQL assign → Copilot, baseRef=feature/issue-{N},
       customAgent=$PRD_AGENT (optional), prompt: write
       docs/planning/{N}/prd.md
   poll for Copilot's PR → enable auto-merge (squash + delete branch)
        │ (you approve → auto-merge)
        ▼
[02-prd-merged]
   trigger: PR merged into feature/issue-*, paths=docs/planning/*/prd.md
   file child issue "Design doc for #N"
   GraphQL assign → Copilot, baseRef=feature/issue-{N},
       customAgent=$DESIGN_AGENT (optional), prompt: write design.md
   enable auto-merge on the resulting PR
        │ (you approve → auto-merge)
        ▼
[03-design-merged]
   trigger: PR merged, paths=docs/planning/*/design.md
   checkout feature/issue-{N}, branch feature/issue-{N}/impl
   npm i -g @github/copilot
   ./src/ralph.bash --file docs/planning/{N}/design.md
                    --model $IMPL_MODEL --iterations 15
   on DONE: push, open PR → feature/issue-{N}, enable auto-merge
        │ (you approve → auto-merge)
        ▼
[04-impl-merged]
   trigger: PR merged, head=feature/issue-*/impl, base=feature/issue-*
   open PR feature/issue-{N} → main, enable auto-merge
        │ (you approve → auto-merge → done)
```

## Quickstart

1. Use this template (or fork it). The pipeline lives in `.github/workflows/`.
2. Generate a user PAT and add it as repo secret `COPILOT_USER_PAT`. **See [Generating the user PAT](#generating-the-user-pat) below — this is the part that doesn't work without setup.**
3. (Optional) Set repo variables for per-stage model pinning. See [Optional: per-stage model pinning](#optional-per-stage-model-pinning).
4. Create the label `ralph:start` (Issues → Labels → New).
5. File an issue using the **Feature request** template. Apply the `ralph:start` label.
6. Approve each PR as it lands. The next stage kicks off automatically.

## Generating the user PAT

The cloud Copilot coding agent is **billed per user**, so GitHub doesn't let bots — including the default `GITHUB_TOKEN` and any GitHub App — summon it. The pipeline therefore needs a personal access token that *you* own, stored as a repo secret named `COPILOT_USER_PAT`.

You have two PAT formats to choose from. Fine-grained is preferred (least privilege, expires).

### Option A — Fine-grained PAT (preferred)

1. Go to <https://github.com/settings/personal-access-tokens/new>.
2. **Token name**: something obvious, e.g. `ralph-pipeline-<reponame>`.
3. **Resource owner**: pick the user/org that owns this repo.
4. **Expiration**: pick the longest your security policy allows (90d is a reasonable default).
5. **Repository access** → *Only select repositories* → pick this repo.
6. **Repository permissions** — set these to **Read and write**:

   | Permission       | Access         | Why                                      |
   |------------------|----------------|------------------------------------------|
   | Actions          | Read and write | Trigger downstream workflows             |
   | Contents         | Read and write | Push branches, commit ralph's output     |
   | Issues           | Read and write | Assign Copilot, file child issues        |
   | Pull requests    | Read and write | Open PRs, enable auto-merge              |
   | Metadata         | Read-only      | (auto-included)                          |

7. **Account permissions** — set this to **Read and write**:

   | Permission        | Access         | Why                                       |
   |-------------------|----------------|-------------------------------------------|
   | Copilot Requests  | Read and write | Copilot CLI in stage 03 needs this        |

8. Generate the token. **Copy it immediately** — you won't see it again.

### Option B — Classic PAT (fallback)

If your org disables fine-grained PATs or you need org-wide scope:

1. Go to <https://github.com/settings/tokens/new> (classic).
2. Scopes: `repo` (full), `read:org`, `gist`. The Copilot CLI also expects `workflow` if you want it to be able to edit workflow files (probably not needed here).
3. Generate, copy.

### Storing the PAT

1. In this repo, go to **Settings → Secrets and variables → Actions → New repository secret**.
2. Name: `COPILOT_USER_PAT`. Value: paste the token. Save.

If you're using this template across multiple repos in an org, make it an **organization secret** instead (Org settings → Secrets and variables → Actions) and grant it to the relevant repos.

## Optional: per-stage model pinning

By default the cloud Copilot stages (PRD, design) use the repo's default Copilot model, and the implementation stage uses `claude-sonnet-4-6` via Copilot CLI. To pin specific models per stage you have two knobs:

### For PRD and design (cloud agent stages)

Per-task model selection on the cloud coding agent is only exposed through **custom agents**, which require Copilot Pro+ or Enterprise. If you have one of those:

1. Create three custom agents in your Copilot settings, each pinned to a model:
   - one on `claude-haiku-4-5` (or similar) — say, slug `prd-haiku`
   - one on `claude-opus-4-7` — say, slug `design-opus`
   - the impl stage doesn't need one (it uses CLI directly)
2. In this repo: **Settings → Secrets and variables → Actions → Variables tab**:
   - `PRD_AGENT` = `prd-haiku`
   - `DESIGN_AGENT` = `design-opus`

If these vars are unset, the workflows skip `customAgent` entirely and Copilot uses the repo default. Everything still works, just on whatever the default is.

### For implementation (CLI stage)

The implementation stage runs Copilot CLI in-runner, which accepts `--model` natively without custom agents. To override:

- `IMPL_MODEL` repo variable. Default: `claude-sonnet-4-6`. Valid values are whatever the installed `@github/copilot` CLI accepts.
- `IMPL_ITERATIONS` repo variable. Default: `15`. Caps how many times ralph loops before failing loud.

## Repo settings checklist

- [x] `COPILOT_USER_PAT` secret set
- [x] Label `ralph:start` exists
- [ ] Branch protection on `main`: require 1 approving review, dismiss stale, require status checks. Auto-merge needs this — without protection, "auto-merge" merges immediately on creation.
- [ ] Branch protection on `feature/issue-*` (recommended): same rules. This is what gives you the human gate at every stage.
- [ ] Verify Copilot tier supports the coding agent (Pro / Pro+ / Business / Enterprise — free does not).
- [ ] If your org has rulesets that block bot pushes, add `copilot-swe-agent[bot]` to the bypass list — otherwise Copilot can't push to its branch.

## How the pieces fit

```
.github/
├── ISSUE_TEMPLATE/feature.yml      issue form that feeds the PRD agent
├── workflows/
│   ├── 01-on-label.yml             label → trunk + assign Copilot for PRD
│   ├── 02-prd-merged.yml           PRD merged → assign Copilot for design
│   ├── 03-design-merged.yml        design merged → ralph loop for impl
│   ├── 04-impl-merged.yml          impl merged → trunk → main PR
│   └── check.yml                   self-CI: actionlint + shellcheck on PRs
scripts/
├── assign-copilot.sh               GraphQL replaceActorsForAssignable wrapper
├── enable-automerge.sh             poll for Copilot's PR, enable auto-merge
├── parse-issue-number.sh           pull {N} out of feature/issue-{N}[/...]
├── render-template.sh              envsubst wrapper for the prompt templates
└── prompts/
    ├── prd.md.tmpl
    ├── design.md.tmpl
    └── impl-task.md.tmpl
src/
└── ralph.bash                      vendored ralph loop (CC0 from exokomodo/im-gonna-ralph)
```

Each stage's handoff is a path-filtered PR-merged trigger — declarative, crash-safe, re-runnable. The only state between stages is the issue number, which lives in the branch name.

## Troubleshooting

**Copilot never opens a PR.** Check Actions logs for stage 01 — most common cause is a PAT scope issue or Copilot tier (free can't use the coding agent). If `assign-copilot.sh` succeeded but no PR appears within 15 min, check Copilot's status page and your org's Copilot settings.

**Auto-merge doesn't fire after approval.** Auto-merge requires branch protection that demands at least one approval and/or passing checks. Without protection rules, GitHub merges immediately when auto-merge is enabled (which is probably not what you want), and if no protection rules apply, "auto-merge" can also be a no-op. Set up branch protection on `main` and `feature/issue-*`.

**Stage 02 / 03 / 04 doesn't trigger after a merge.** The path-filtered triggers only fire when the PR's diff includes the right path. If a Copilot PR merged but didn't add the expected file (e.g. wrote to the wrong path), the next workflow won't fire. Fix the path in Copilot's PR before merging.

**ralph hits the iteration cap without completion.** Stage 03 fails loud at 15 iterations. Read `.ralph/<timestamp>/iteration_*.txt` from the failed run's logs — common causes: design doc was too vague, tests are flaky, environment is missing a dependency. Fix the input and re-run by re-merging the design PR (or push an empty commit to the trunk that touches `docs/planning/{N}/design.md`).

**Workflows don't see secrets / vars.** GitHub doesn't expose `secrets` or `vars` to PRs from forks. This pipeline assumes the issue and downstream PRs all live in the canonical repo, which is the standard mode for trunk-based development.

## Development on this template itself

```bash
make setup    # install actionlint + shellcheck
make test     # run both linters
```

`check.yml` runs `make test` on PRs into the template's own `main`, so the template stays clean.

## Acknowledgments

- The implementation loop (`src/ralph.bash`) is vendored verbatim from [`exokomodo/im-gonna-ralph`](https://github.com/exokomodo/im-gonna-ralph), which is licensed under [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/) (public-domain dedication). Attribution is preserved in the file's header comment.
- That project credits [Geoffrey Huntley's loop write-up](https://ghuntley.com/loop/) and [this Tavernari gist](https://gist.github.com/Tavernari/01d21584f8d4d8ccea8ceca305656ab3).
- GitHub Copilot coding agent: [docs](https://docs.github.com/copilot/concepts/agents/coding-agent/about-coding-agent).

## License

This project is [Apache 2.0](LICENSE). The vendored `src/ralph.bash` is CC0-1.0 from upstream — CC0 is a public-domain dedication, so it imposes no obligations on this project's licensing; the header comment in that file preserves the attribution.
