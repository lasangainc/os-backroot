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

br8_bootscreen_png_ok() {
    _p="$1"
    [ -r "$_p" ] || return 1
    _sz=$(wc -c <"$_p" 2>/dev/null) || return 1
    [ "$_sz" -ge 64 ] 2>/dev/null || return 1
    _sig=$(dd if="$_p" bs=8 count=1 2>/dev/null) || return 1
    [ "$_sig" = "$(printf '\211PNG\r\n\032\n')" ] || return 1
    _ct=$(dd if="$_p" bs=1 skip=25 count=1 2>/dev/null | od -An -tu1 | tr -d ' ')
    [ "$_ct" = "2" ] || [ "$_ct" = "6" ] || return 1
    return 0
}

br8_bootscreen_ppm_ok() {
    _p="$1"
    [ -r "$_p" ] || return 1
    _sz=$(wc -c <"$_p" 2>/dev/null) || return 1
    [ "$_sz" -ge 32 ] 2>/dev/null || return 1
    _sig=$(dd if="$_p" bs=2 count=1 2>/dev/null) || return 1
    [ "$_sig" = "P6" ] || return 1
    return 0
}

br8_pick_bootscreen() {
    for _p in \
        /usr/share/backroot8/bootscreen.ppm \
        /usr/share/backroot8/bootscreen.png \
        /usr/share/plymouth/themes/backroot8/bootscreen.png
    do
        case "$_p" in
            *.ppm)
                br8_bootscreen_ppm_ok "$_p" && { echo "$_p"; return 0; }
                ;;
            *.png)
                br8_bootscreen_png_ok "$_p" && { echo "$_p"; return 0; }
                ;;
        esac
    done
    return 1
}
