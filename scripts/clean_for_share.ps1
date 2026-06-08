# Prepare this tree for zip/backup/share: remove build caches and generated sources.
# Usage: .\scripts\clean_for_share.ps1
# Next build: pio run -e esp32_s3_epaper_397  (regenerates I18n + web HTML)

$ErrorActionPreference = "SilentlyContinue"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

function Get-TreeSizeBytes {
  param([string]$Path = $Root)
  (Get-ChildItem $Path -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
}

$before = Get-TreeSizeBytes
Write-Host ("Size before: {0:N1} MB" -f ($before / 1MB))
Write-Host ""

Write-Host "Removing PlatformIO / IDE caches ..."
Remove-Item -Recurse -Force .pio, .cache, .vscode, .idea, .cursor

Write-Host "Removing optional vendor dumps (not required to build) ..."
Remove-Item -Recurse -Force .waveshare-ref, "docs\ESP32-S3-ePaper-3.97"

Write-Host "Removing generated sources (recreated on next pio run) ..."
Remove-Item -Force `
  "src\network\html\*.generated.h", `
  "src\network\html\js\*.generated.h", `
  "lib\I18n\I18nKeys.h", "lib\I18n\I18nStrings.cpp", "lib\I18n\I18nStrings.h"

Write-Host "Removing dev-only files ..."
Remove-Item -Recurse -Force canvases, agent-transcripts -ErrorAction SilentlyContinue
Remove-Item -Force "scripts\sync_to_397_port.sh", "fix-cursor-encode issue.txt" -ErrorAction SilentlyContinue
Remove-Item -Force "platformio.local.ini", "*.local.ini", "*.code-workspace", "firmware.bin", "firmware.elf" -ErrorAction SilentlyContinue
Get-ChildItem -Force -Filter "fix-cursor-encode*.txt" | Remove-Item -Force
Get-ChildItem -Force -Filter "*.zip", "*.7z" | Remove-Item -Force

Get-ChildItem -Recurse -Directory -Filter __pycache__ | Remove-Item -Recurse -Force
Get-ChildItem -Recurse -File -Filter "*.pyc" | Remove-Item -Force
Get-ChildItem -Recurse -File -Filter ".DS_Store" | Remove-Item -Force
Get-ChildItem -Recurse -File -Filter "Thumbs.db" | Remove-Item -Force
Get-ChildItem -Recurse -File -Filter "*.log" | Remove-Item -Force
if (Test-Path "open-x4-sdk\.git") { Remove-Item -Recurse -Force "open-x4-sdk\.git" }

$after = Get-TreeSizeBytes
Write-Host ""
Write-Host ("Size after:  {0:N1} MB (freed ~{1:N1} MB)" -f ($after / 1MB), (($before - $after) / 1MB))
Write-Host ""
Write-Host "Ready to zip for backup. Recipients need PlatformIO +:"
Write-Host "  pip install -r scripts/requirements.txt"
Write-Host "  pio run -e esp32_s3_epaper_397"
