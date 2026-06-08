#!/usr/bin/env bash
# Prepare this tree for zip/git share: remove build caches and generated sources.
# Same as clean_workspace.sh (kept for parity with the 397 port repo).
# Usage: ./scripts/clean_for_share.sh

set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/clean_workspace.sh"
