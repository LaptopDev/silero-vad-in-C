#!/bin/bash
# This script plays my last recording
# keeps it open
# To do: Will show transcription on screen as well
 

new () {
    local dir="${2:-.}"
    [[ -d $dir ]] || { echo "Error: '$dir' not a dir" >&2; return 1; }
    find "$dir" -maxdepth 1 -type f -printf '%T@ %p\n' |
        sort -nr |
        head -n "${1:-}" |
        cut -d' ' -f2- |
        while IFS= read -r f; do realpath "$f"; done
}

PLAYBACK_FILE="$(cd /home/user/.transcription && new 3 | grep .wav)"

# --- 1) launch mpv ---
mpv --input-ipc-server=/tmp/mpvsock --script=~/.config/mpv/scripts/overlay-status.lua --script=~/.config/mpv/scripts/hardboundary.lua --keep-open=yes  "$PLAYBACK_FILE" &

MPV_PID=$!

# --- 2) start status loop inside this script ---
PLAYBACK_STATE_FILE=/tmp/player_status.txt
echo hello > /tmp/player_status.txt

# --- 3) launch overlay to show it ---
/home/user/source/git/x11-overlay/bin/overlay -o SE -s 19 -e 50 /tmp/player_status.txt &
OVERLAY_PID=$!

# --- 4) persistent blocking notification ---
dunstify -u low -t 0 -b "Recording" "Scroll to seek with media keys"

# --- 5) cleanup when dismissed ---
kill "$STATUS_PID"
kill "$OVERLAY_PID"
kill "$MPV_PID"
