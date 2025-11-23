#!/bin/sh
DELTA="$1"
CUR=$(playerctl position 2>/dev/null) || exit 1
CLEAN_DELTA=${DELTA#+}
NEW=$(printf '%s\n' "$CUR + $CLEAN_DELTA" | bc | cut -d. -f1)

# use relative delta for mpv
if playerctl -l 2>/dev/null | grep -qx mpv; then
    # sanitize delta
    CLEAN_DELTA=${DELTA#+}
    CLEAN_DELTA=$(printf "%d" "$CLEAN_DELTA")

    # Relies upon /home/user/.config/mpv/scripts/hardboundary.lua
    printf '{ "command": ["script-message", "hardseek-relative", "%d"] }\n' "$CLEAN_DELTA" \
        | socat - /tmp/mpvsock

#    printf '{ "command": ["seek", %d, "relative"] }\n' "$CLEAN_DELTA" \
#        | socat - /tmp/mpvsock

    exit 0
fi

# else: pick ANY running non-mpv player
OTHER=$(playerctl -l 2>/dev/null | grep -v '^mpv$' | head -n1)

if [ -n "$OTHER" ]; then
    playerctl -p "$OTHER" position "$NEW"
    exit 0
fi

# nothing else available
exit 1
