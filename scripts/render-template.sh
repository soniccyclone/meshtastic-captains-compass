#!/usr/bin/env bash
#
# render-template.sh — substitute a known set of environment variables in a
# template file and print the result to stdout. Avoids dependence on envsubst
# (gettext) so it works on a stock macOS box too.
#
# Usage:
#   ENV_VAR=value render-template.sh <template-path>
#
# We only expand a fixed allowlist of names so that braces in the template
# body that aren't ours pass through untouched.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <template-path>" >&2
  exit 64
fi

template="$1"
if [[ ! -f "${template}" ]]; then
  echo "error: template not found: ${template}" >&2
  exit 1
fi

ALLOWED=(ISSUE_NUMBER ISSUE_TITLE ISSUE_BODY PRD_PATH DESIGN_PATH PARENT_ISSUE BASE_REF)

content="$(cat "${template}")"

for name in "${ALLOWED[@]}"; do
  # Default unset/empty to empty string (so ${FOO} renders as "" rather than literal).
  value="${!name-}"
  # Replace every occurrence of ${NAME} with $value.
  content="${content//\$\{${name}\}/${value}}"
done

printf '%s\n' "${content}"
