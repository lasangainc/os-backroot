#!/bin/bash
# First-run account creation (invoked from br8-oobe).
set -euo pipefail

USER_NAME="${1:-}"
PASS_FILE="${2:-}"

[[ -n "$USER_NAME" && -f "$PASS_FILE" ]] || exit 1
[[ -f /etc/backroot8/oobe-pending ]] || exit 0

mkdir -p /run/br8-oobe
chmod 1777 /run/br8-oobe

if ! id "$USER_NAME" &>/dev/null; then
    useradd -m -G wheel,audio,video,storage -s /bin/bash "$USER_NAME"
fi

PASS="$(tr -d '\r\n' <"$PASS_FILE")"
if [[ -n "$PASS" ]]; then
    echo "$USER_NAME:$PASS" | chpasswd
else
    passwd -d "$USER_NAME" 2>/dev/null || true
fi

mkdir -p /etc/sudoers.d
echo "%wheel ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/br8-wheel
chmod 440 /etc/sudoers.d/br8-wheel

mkdir -p "/home/$USER_NAME/.config/backroot8"
WP_IDX=0
if [[ -f /run/br8-oobe/wallpaper ]]; then
    WP_IDX="$(tr -dc '0-9' </run/br8-oobe/wallpaper)"
fi
WP_IDX=$((WP_IDX + 1))
WP_SRC="/usr/share/backroot8/oobe-wallpapers/wallpaper-${WP_IDX}.jpg"
if [[ -r "$WP_SRC" ]]; then
    install -Dm644 "$WP_SRC" "/home/$USER_NAME/.config/backroot8/wallpaper.jpg"
fi
chown -R "$USER_NAME:$USER_NAME" "/home/$USER_NAME"

mkdir -p /etc/backroot8
echo "$USER_NAME" > /etc/backroot8/desktop-user
chmod 644 /etc/backroot8/desktop-user

mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf <<EOF
[Service]
ExecStart=
ExecStart=-/usr/bin/agetty --autologin ${USER_NAME} --noclear %I \$TERM
EOF

cat > /home/"$USER_NAME"/.xinitrc <<'EOF'
#!/bin/sh
exec /etc/X11/xinit/xinitrc
EOF
chown "$USER_NAME:$USER_NAME" "/home/$USER_NAME/.xinitrc"
chmod +x "/home/$USER_NAME/.xinitrc"

loginctl enable-linger "$USER_NAME" 2>/dev/null || true
runuser -u "$USER_NAME" -- fc-cache -f 2>/dev/null || true

rm -f /run/br8-oobe/panel-ready
touch /run/br8-oobe/keep-loading

rm -f /etc/backroot8/oobe-pending
touch /etc/backroot8/oobe-complete
