#!/usr/bin/env bash
#
# assign-copilot.sh — assign the Copilot coding agent to an issue with a custom
# base branch and (optionally) a custom-agent slug. Uses the GraphQL
# replaceActorsForAssignable mutation, which is the only path that exposes
# agentAssignment.baseRef and agentAssignment.customAgent.
#
# Usage:
#   assign-copilot.sh <ISSUE_NUMBER> <BASE_REF> <PROMPT_FILE> [CUSTOM_AGENT_SLUG]
#
# Env:
#   GH_TOKEN — must be a user-owned PAT (GITHUB_TOKEN cannot summon Copilot)
#   GH_REPO  — owner/repo (auto-set inside Actions)

# shellcheck disable=SC2016
# (single-quoted $vars in GraphQL queries are intentional — they're query
# variables interpreted server-side, not shell expansions.)

set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "usage: $0 <ISSUE_NUMBER> <BASE_REF> <PROMPT_FILE> [CUSTOM_AGENT_SLUG]" >&2
  exit 64
fi

ISSUE_NUMBER="$1"
BASE_REF="$2"
PROMPT_FILE="$3"
CUSTOM_AGENT="${4:-}"

if [[ -z "${GH_TOKEN:-}" ]]; then
  echo "error: GH_TOKEN is not set. Copilot can only be summoned by a user-owned PAT — see README." >&2
  exit 1
fi

if [[ ! -f "${PROMPT_FILE}" ]]; then
  echo "error: prompt file not found: ${PROMPT_FILE}" >&2
  exit 1
fi

OWNER="${GH_REPO%%/*}"
REPO="${GH_REPO##*/}"
if [[ -z "${OWNER}" || -z "${REPO}" ]]; then
  echo "error: GH_REPO must be 'owner/repo' (got '${GH_REPO:-<unset>}')" >&2
  exit 1
fi

PROMPT_BODY="$(cat "${PROMPT_FILE}")"

issue_id=$(gh api graphql \
  -f query='query($o:String!,$r:String!,$n:Int!){repository(owner:$o,name:$r){issue(number:$n){id}}}' \
  -F o="${OWNER}" -F r="${REPO}" -F n="${ISSUE_NUMBER}" \
  --jq '.data.repository.issue.id')

if [[ -z "${issue_id}" || "${issue_id}" == "null" ]]; then
  echo "error: could not resolve issue #${ISSUE_NUMBER} in ${OWNER}/${REPO}" >&2
  exit 1
fi

copilot_id=$(gh api graphql \
  -f query='query($o:String!,$r:String!){repository(owner:$o,name:$r){suggestedActors(loginNames:["copilot"], capabilities:[CAN_BE_ASSIGNED], first:1){nodes{login ... on Bot {id}}}}}' \
  -F o="${OWNER}" -F r="${REPO}" \
  --jq '.data.repository.suggestedActors.nodes[0].id')

if [[ -z "${copilot_id}" || "${copilot_id}" == "null" ]]; then
  echo "error: Copilot is not available as an assignable actor on ${OWNER}/${REPO}." >&2
  echo "  Check Copilot tier (Pro/Pro+/Business/Enterprise) and that the coding agent is enabled." >&2
  exit 1
fi

agent_assignment_json=$(jq -n \
  --arg baseRef "${BASE_REF}" \
  --arg instructions "${PROMPT_BODY}" \
  --arg customAgent "${CUSTOM_AGENT}" \
  '{baseRef: $baseRef, customInstructions: $instructions} + (if $customAgent == "" then {} else {customAgent: $customAgent} end)')

input_json=$(jq -n \
  --arg assignableId "${issue_id}" \
  --arg actorId "${copilot_id}" \
  --argjson agentAssignment "${agent_assignment_json}" \
  '{assignableId: $assignableId, actorIds: [$actorId], agentAssignment: $agentAssignment}')

echo "Assigning Copilot to issue #${ISSUE_NUMBER}, baseRef=${BASE_REF}${CUSTOM_AGENT:+, customAgent=${CUSTOM_AGENT}}"

gh api graphql \
  -H "GraphQL-Features: issues_copilot_assignment_api_support" \
  -f query='mutation($input: ReplaceActorsForAssignableInput!) { replaceActorsForAssignable(input: $input) { assignable { ... on Issue { number url } } } }' \
  --raw-field "input=${input_json}" \
  >/dev/null

echo "Assigned. Copilot will open a draft PR shortly."
