#!/usr/bin/env bash
#
# enable-automerge.sh — wait for Copilot to open its draft PR against a given
# base branch, then enable auto-merge so the user only has to approve.
#
# Usage:
#   enable-automerge.sh <BASE_REF> [TIMEOUT_SECONDS]
#
# Env:
#   GH_TOKEN — user PAT (Copilot's PRs may need PAT to enable auto-merge if
#              branch protection requires bypassing)
#   GH_REPO  — owner/repo

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <BASE_REF> [TIMEOUT_SECONDS]" >&2
  exit 64
fi

BASE_REF="$1"
TIMEOUT="${2:-600}"
INTERVAL=20
elapsed=0

if [[ -z "${GH_TOKEN:-}" ]]; then
  echo "error: GH_TOKEN is not set" >&2
  exit 1
fi

echo "Waiting up to ${TIMEOUT}s for a Copilot PR against base=${BASE_REF}..."

pr_number=""
while (( elapsed < TIMEOUT )); do
  pr_number=$(gh pr list \
    --base "${BASE_REF}" \
    --author "copilot-swe-agent" \
    --state open \
    --json number \
    --jq '.[0].number // empty')

  if [[ -n "${pr_number}" ]]; then
    break
  fi

  sleep "${INTERVAL}"
  elapsed=$(( elapsed + INTERVAL ))
done

if [[ -z "${pr_number}" ]]; then
  echo "error: no Copilot PR appeared within ${TIMEOUT}s." >&2
  echo "  Check Actions logs and Copilot's status. You can enable auto-merge manually once it shows up." >&2
  exit 1
fi

echo "Found Copilot PR #${pr_number}. Enabling auto-merge (squash, delete branch)."

if ! gh pr merge "${pr_number}" --auto --squash --delete-branch; then
  echo "warn: enabling auto-merge failed. The PR may not be in a state that accepts it yet." >&2
  echo "  You can run \`gh pr merge ${pr_number} --auto --squash --delete-branch\` once Copilot marks it ready." >&2
  exit 0
fi

echo "Auto-merge enabled on PR #${pr_number}."
