# Prepare observations only; installed.wast owns every expected result.
LC_ALL=C
export LC_ALL
printf 'apple 2\nbanana 3\napple 4\n' > /tmp/awk-input
printf 'BEGIN { while ((getline line < "/tmp/awk-input") > 0) n++; close("/tmp/awk-input"); print n; print "saved" > "/tmp/awk-saved"; close("/tmp/awk-saved") }\n' > /tmp/awk-script
/usr/bin/awk 'BEGIN { print "GNU Awk", 6 * 7; printf "%.2f\n", 7 / 2 }' > /tmp/awk-begin
printf '%s\n' "$?" > /tmp/awk-status-begin
/usr/bin/awk '{ count[$1] += $2 } END { print NR, count["apple"], count["banana"] }' /tmp/awk-input > /tmp/awk-array
printf '%s\n' "$?" > /tmp/awk-status-array
/usr/bin/awk '/^a[p]+le[[:space:]]/ { print FNR, NF, toupper($1), $2 }' /tmp/awk-input > /tmp/awk-regex
printf '%s\n' "$?" > /tmp/awk-status-regex
/usr/bin/awk -F: -v prefix=ok 'function twice(x) { return x * 2 } { print prefix, $1, twice($2) }' <<'AWK_INPUT' > /tmp/awk-fields
row:21
AWK_INPUT
printf '%s\n' "$?" > /tmp/awk-status-fields
/usr/bin/awk 'BEGIN { s="a1b22"; print gsub(/[0-9]+/, "X", s), s; n=split("one:two:three", a, ":"); print n, a[2]; print substr("abcdef", 2, 3), index("abcdef", "cd") }' > /tmp/awk-string
printf '%s\n' "$?" > /tmp/awk-status-string
/usr/bin/awk -f /tmp/awk-script > /tmp/awk-getline
printf '%s\n' "$?" > /tmp/awk-status-getline
printf '2\n3\n4\n' | /usr/bin/awk '{ total += $1 } END { print total }' > /tmp/awk-pipe
printf '%s\n' "$?" > /tmp/awk-status-pipe
/usr/bin/awk 'BEGIN { exit 7 }' > /tmp/awk-exit
printf '%s\n' "$?" > /tmp/awk-status-exit
/usr/bin/awk 'BEGIN {' > /tmp/awk-invalid 2> /tmp/awk-error
printf '%s\n' "$?" > /tmp/awk-status-invalid
/usr/bin/awk --version > /tmp/awk-version
printf '%s\n' "$?" > /tmp/awk-status-version
/usr/bin/awk 'BEGIN { printf "%.6f %.6f %.6f %.6f %.6f %.0f %.0f %.1f\n", sin(1), cos(1), atan2(1,1), exp(1), log(2), 2^10, sqrt(9), 5.5%2 }' > /tmp/awk-math
printf '%s\n' "$?" > /tmp/awk-status-math
/usr/bin/awk 'BEGIN { "printf pipe" | getline value; print value, close("printf pipe"); print system("exit 3"); print "written" | "/usr/bin/cat > /tmp/awk-child"; print close("/usr/bin/cat > /tmp/awk-child") }' > /tmp/awk-process
printf '%s\n' "$?" > /tmp/awk-status-process
