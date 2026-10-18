# Display the guest message once when the browser's interactive Bash starts.
if { WASTE_MOTD="$(< /etc/motd)"; } 2>/dev/null; then
  printf '%s\n' "$WASTE_MOTD"
fi
unset WASTE_MOTD
