#!/bin/bash
# First-run account creation (invoked from br8-oobe).
set -euo pipefail

USER_NAME="${1:-}"
PASS_FILE="${2:-}"

[[ -n "$USER_NAME" && -f "$PASS_FILE" ]] || exit 1
[[ -f /etc/backroot8/oobe-pending ]] || exit 0

# Let the loading screen show for a moment.
sleep 4

if ! id "$USER_NAME" &>/dev/null; then
    useradd -m -G wheel,audio,video,storage -s /bin/bash "$USER_NAME"
fi

PASS="$(tr -d '\r\n' <"$PASS_FILE")"
if [[ -n "$PASS" ]]; then
    echo "$USER_NAME:$PASS" | chpasswd
else
    passwd -d "$USER_NAME" 2>/dev/null || true
fi

# Allow sudo for the primary user.
mkdir -p /etc/sudoers.d
echo "%wheel ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/br8-wheel
chmod 440 /etc/sudoers.d/br8-wheel

mkdir -p "/home/$USER_NAME/.config/backroot8"
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

rm -f /etc/backroot8/oobe-pending
touch /etc/backroot8/oobe-complete

# Additional setup time for the loading screen.
sleep 3
