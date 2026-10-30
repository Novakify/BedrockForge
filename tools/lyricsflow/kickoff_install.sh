#!/usr/bin/env bash
# LyricsFlow kickoff: back up + install the lyrics BF/RF DLLs and resources in one go (and roll back).
# Runs in the PC's device shell (Linux VM, mods folder mounted). Nothing is deleted: old files are renamed.
#
# Before running: the game is CLOSED, and the incoming files were committed (device_commit_files) into
#   <mods>/_lyricsflow_incoming/<stamp>/  as  BF-lyrics.dll, RF-lyrics.dll, resources.tar, SHA256SUMS
# (fresh stamp dir every time: re-using a staged name wrote 0 bytes once).
#
# usage: kickoff_install.sh install  <mods dir> <stamp> <cinematic sha>
#        kickoff_install.sh rollback <mods dir> <cinematic sha>
#        kickoff_install.sh status   <mods dir>
set -euo pipefail

cmd=${1:?install|rollback|status}
MODS=${2:?mods dir, e.g. \$HOME/mnt/1.26.51.01/mods}
BF="$MODS/BedrockForgeCore"
RF="$MODS/RendererFoundation"
[ -d "$BF" ] && [ -d "$RF" ] || { echo "not a mods dir with BedrockForgeCore + RendererFoundation: $MODS"; exit 1; }

sz() { stat -c %s "$1" 2>/dev/null || echo 0; }

case "$cmd" in
status)
    ls -la "$BF"/*.dll "$RF"/*.dll
    ls "$BF/resources/lyricsflow" 2>/dev/null || echo "(no resources/lyricsflow yet)"
    ;;
install)
    STAMP=${3:?stamp}; CINE=${4:?cinematic sha of the currently installed RF}
    IN="$MODS/_lyricsflow_incoming/$STAMP"
    for f in BF-lyrics.dll RF-lyrics.dll resources.tar SHA256SUMS; do
        [ "$(sz "$IN/$f")" -gt 0 ] || { echo "missing or 0-byte: $IN/$f"; exit 1; }
    done
    (cd "$IN" && sha256sum -c SHA256SUMS) || { echo "checksum mismatch, not installing"; exit 1; }

    # 1) back up (rename, never overwrite an existing backup)
    bfb="$BF/BedrockForgeCore.pre-lyrics.dll"
    rfb="$RF/RendererFoundation.pre-lyrics-$CINE.dll"
    if [ -e "$bfb" ]; then echo "BF backup already there ($bfb), keeping it"; else cp -p "$BF/BedrockForgeCore.dll" "$bfb"; fi
    if [ -e "$rfb" ]; then echo "RF backup already there ($rfb), keeping it"; else cp -p "$RF/RendererFoundation.dll" "$rfb"; fi
    [ "$(sz "$bfb")" -gt 0 ] && [ "$(sz "$rfb")" -gt 0 ] || { echo "backup failed"; exit 1; }

    # 2) install DLLs (cp over the live name: replaces contents, no delete needed)
    cp "$IN/BF-lyrics.dll" "$BF/BedrockForgeCore.dll"
    cp "$IN/RF-lyrics.dll" "$RF/RendererFoundation.dll"

    # 3) resources/lyricsflow (fonts, presets, licences); an existing user config is never touched
    tmp=$(mktemp -d)
    tar -xf "$IN/resources.tar" -C "$tmp"
    src=$(find "$tmp" -type d -path '*resources/lyricsflow' | head -1)
    [ -n "$src" ] || { echo "resources.tar has no resources/lyricsflow"; exit 1; }
    mkdir -p "$BF/resources/lyricsflow"
    cp -r "$src"/. "$BF/resources/lyricsflow/"

    echo "installed:"
    echo "  BF $(sz "$BF/BedrockForgeCore.dll") bytes (backup $(basename "$bfb"))"
    echo "  RF $(sz "$RF/RendererFoundation.dll") bytes (backup $(basename "$rfb"))"
    echo "  fonts: $(ls "$BF/resources/lyricsflow/fonts" | grep -c '\.png$') atlases, presets: $(ls "$BF/resources/lyricsflow/presets" | tr '\n' ' ')"
    ;;
rollback)
    CINE=${3:?cinematic sha used at install}
    bfb="$BF/BedrockForgeCore.pre-lyrics.dll"
    rfb="$RF/RendererFoundation.pre-lyrics-$CINE.dll"
    [ "$(sz "$bfb")" -gt 0 ] && [ "$(sz "$rfb")" -gt 0 ] || { echo "backups not found: $bfb / $rfb"; exit 1; }
    cp "$bfb" "$BF/BedrockForgeCore.dll"
    cp "$rfb" "$RF/RendererFoundation.dll"
    echo "rolled back to the pre-lyrics DLLs (backups kept). resources/lyricsflow left in place (inert without the module)."
    ;;
*) echo "unknown command $cmd"; exit 2 ;;
esac
