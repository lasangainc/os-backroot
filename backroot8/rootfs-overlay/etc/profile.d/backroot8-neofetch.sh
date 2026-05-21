# Show system info on interactive login (SSH / console)
if [ -n "${PS1:-}" ] && [ -t 1 ] && [ -z "${NEOFETCH_RUN:-}" ] && command -v neofetch >/dev/null 2>&1; then
    export NEOFETCH_RUN=1
    neofetch --config /etc/neofetch/config.conf
fi
