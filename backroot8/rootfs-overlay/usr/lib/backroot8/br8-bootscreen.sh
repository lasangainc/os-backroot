#!/bin/sh
# Boot splash helpers (initramfs ash + systemd /bin/sh).

br8_verbose() {
    grep -q 'br8\.debug' /proc/cmdline
}

br8_log() {
    if br8_verbose; then
        echo "backroot8: $*" >/dev/console
    fi
}

# True when $1 is a complete 8-bit RGB PNG (fbi/ida-friendly).
br8_bootscreen_png_ok() {
    _p="$1"
    [ -r "$_p" ] || return 1
    _sz=$(wc -c <"$_p" 2>/dev/null) || return 1
    [ "$_sz" -ge 64 ] 2>/dev/null || return 1
    _sig=$(dd if="$_p" bs=8 count=1 2>/dev/null) || return 1
    [ "$_sig" = "$(printf '\211PNG\r\n\032\n')" ] || return 1
    _hdr=$(dd if="$_p" bs=1 skip=16 count=10 2>/dev/null | od -An -tu1 | tr -s ' ' '\n' | tr -d ' ')
    # IHDR: width height bit_depth color_type
    set -- $_hdr
    [ "${4:-}" = "8" ] && [ "${5:-}" = "2" ] || return 1
    return 0
}

# Print the first usable boot splash PNG path, or nothing.
br8_pick_bootscreen() {
    for _p in \
        /usr/share/backroot8/bootscreen.png \
        /usr/share/plymouth/themes/backroot8/bootscreen.png
    do
        br8_bootscreen_png_ok "$_p" && { echo "$_p"; return 0; }
    done
    return 1
}

# Run fbi on the framebuffer without forcing /dev/fb0 (Plymouth may own DRM).
# https://stackoverflow.com/questions/46143476/why-does-fbi-not-show-splash-image-during-system-startup
br8_fbi_show() {
    _img="$1"
    _bg="${2:-0}"
    if [ "$_bg" = "1" ]; then
        fbi --noverbose -a "$_img" </dev/null 2>/dev/null &
        return 0
    fi
    fbi -T 1 --noverbose -a "$_img" </dev/null 2>/dev/null &
}
