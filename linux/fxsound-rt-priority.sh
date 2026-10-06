#!/bin/sh
# Best-effort RTKit promotion for FxSound's PipeWire realtime data loops.
# No root privileges or persistent system policy changes are required.

command -v busctl >/dev/null 2>&1 || exit 0
command -v ps >/dev/null 2>&1 || exit 0
command -v pgrep >/dev/null 2>&1 || exit 0

pid="$(pgrep -n -x fxsoundd 2>/dev/null || true)"
[ -n "$pid" ] && [ -r "/proc/$pid/status" ] || exit 0

max_prio="$(
  busctl --system get-property     org.freedesktop.RealtimeKit1     /org/freedesktop/RealtimeKit1     org.freedesktop.RealtimeKit1     MaxRealtimePriority 2>/dev/null |
  awk '{print $2}'
)"
case "$max_prio" in
  ''|*[!0-9]*) exit 0 ;;
esac
[ "$max_prio" -gt 0 ] || exit 0

prio="$max_prio"
[ "$prio" -gt 20 ] && prio=20

threads=""
i=0
while [ "$i" -lt 30 ]; do
  threads="$(
    ps -L -p "$pid" -o tid=,comm= 2>/dev/null |
      awk '$2 ~ /^data-loop/ {print $1}'
  )"
  [ -n "$threads" ] && break
  sleep 0.05
  i=$((i + 1))
done
[ -n "$threads" ] || exit 0

promoted=0
for tid in $threads; do
  if busctl --system call       org.freedesktop.RealtimeKit1       /org/freedesktop/RealtimeKit1       org.freedesktop.RealtimeKit1       MakeThreadRealtimeWithPID       ttu "$pid" "$tid" "$prio" >/dev/null 2>&1; then
    promoted=$((promoted + 1))
  fi
done

[ "$promoted" -gt 0 ] &&
  echo "FxSound RTKit: promoted $promoted data-loop thread(s) to realtime priority $prio"
exit 0