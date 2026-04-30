#!/usr/bin/env bash
#
# parse-issue-number.sh — extract the issue number N from a feature branch ref
# of the form `feature/issue-{N}` or `feature/issue-{N}/something`.
#
# Usage: parse-issue-number.sh <BRANCH_REF>

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <BRANCH_REF>" >&2
  exit 64
fi

ref="$1"

if [[ "${ref}" =~ ^feature/issue-([0-9]+)(/.*)?$ ]]; then
  echo "${BASH_REMATCH[1]}"
else
  echo "error: ref '${ref}' does not match feature/issue-{N}[/...]" >&2
  exit 1
fi
