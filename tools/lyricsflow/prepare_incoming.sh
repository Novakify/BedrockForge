#!/usr/bin/env bash
# Cloud side of the LyricsFlow install: pull the CI builds (git, since Actions artifact downloads are blocked) and
# pack them for kickoff_install.sh. Output: <out>/<stamp>/{BF-lyrics.dll,RF-lyrics.dll,resources.tar,SHA256SUMS,BUILDS}
# usage: prepare_incoming.sh <repo clone> <out dir> [stamp]
set -euo pipefail
REPO=${1:?repo clone}; OUT=${2:?out dir}; STAMP=${3:-$(date -u +%Y%m%d-%H%M%S)}
cd "$REPO"
git fetch -q origin +lyricsflow-builds:refs/remotes/origin/lyricsflow-builds +lyricsflow-rf-builds:refs/remotes/origin/lyricsflow-rf-builds
D="$OUT/$STAMP"; mkdir -p "$D"
git show origin/lyricsflow-builds:BedrockForgeCore/BedrockForgeCore.dll > "$D/BF-lyrics.dll"
git show origin/lyricsflow-rf-builds:RendererFoundation/RendererFoundation.dll > "$D/RF-lyrics.dll"
git archive --format=tar origin/lyricsflow-builds BedrockForgeCore/resources/lyricsflow > "$D/resources.tar"
{ echo "BF build: $(git log -1 --format='%h %s' origin/lyricsflow-builds)"
  echo "RF build: $(git log -1 --format='%h %s' origin/lyricsflow-rf-builds)"; } > "$D/BUILDS"
(cd "$D" && sha256sum BF-lyrics.dll RF-lyrics.dll resources.tar > SHA256SUMS)
cat "$D/BUILDS"; ls -la "$D"
echo "stamp: $STAMP"
