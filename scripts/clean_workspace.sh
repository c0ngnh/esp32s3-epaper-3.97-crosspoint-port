#!/usr/bin/env bash
# Remove build caches, optional vendor dumps, and generated sources from the working tree.
# Safe to run anytime — next `pio run` regenerates what is needed.
#
# Usage: ./scripts/clean_workspace.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "Removing PlatformIO build output, IDE cache, and local settings ..."
rm -rf .pio .cache .vscode .idea .cursor

echo "Removing optional local vendor dumps (re-download from Waveshare wiki if needed) ..."
rm -rf .waveshare-ref
rm -rf docs/ESP32-S3-ePaper-3.97

echo "Removing dev-only files ..."
rm -f scripts/sync_to_397_port.sh
rm -f fix-cursor-encode*.txt platformio.local.ini *.local.ini
rm -f firmware.bin firmware.elf
find . -maxdepth 1 -name '*.code-workspace' -delete 2>/dev/null || true

echo "Removing generated sources (recreated on next pio run) ..."
rm -f src/network/html/*.generated.h
rm -f src/network/html/js/*.generated.h
rm -f lib/I18n/I18nKeys.h lib/I18n/I18nStrings.cpp lib/I18n/I18nStrings.h

echo "Removing Python / OS junk ..."
find . -type d -name '__pycache__' -not -path './.pio/*' -exec rm -rf {} + 2>/dev/null || true
find . -name '.DS_Store' -not -path './.pio/*' -delete 2>/dev/null || true
find . -name '*.pyc' -not -path './.pio/*' -delete 2>/dev/null || true
find . -name 'Thumbs.db' -not -path './.pio/*' -delete 2>/dev/null || true
find . -name '*.log' -not -path './.pio/*' -delete 2>/dev/null || true

# Nested git dirs inside dependencies confuse some tools; strip if present after .pio is gone
rm -rf open-x4-sdk/.git 2>/dev/null || true

echo ""
echo "Workspace clean. Top-level size:"
du -sh "$ROOT" --exclude='.git' 2>/dev/null || du -sh "$ROOT"
echo "Rebuild: pio run -e esp32_s3_epaper_397"
