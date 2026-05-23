#!/bin/sh
# True when running from the Backroot 8 live ISO/USB (backroot8iso on cmdline).
grep -q 'backroot8iso' /proc/cmdline 2>/dev/null
