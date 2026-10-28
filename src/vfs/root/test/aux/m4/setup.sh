# Prepare observations only; all expected results belong to installed.wast.
LC_ALL=C
export LC_ALL
/usr/bin/m4 <<'M4_INPUT' > /tmp/m4-macro
define(`join', `$1:$2')dnl
join(`left', `right')
M4_INPUT
printf '%s\n' "$?" > /tmp/m4-status-macro
/usr/bin/m4 <<'M4_INPUT' > /tmp/m4-quote
define(`argc', `$#')dnl
argc(a, `b,c', d)
changequote([,])dnl
define([x], [quoted])dnl
x [x]
M4_INPUT
printf '%s\n' "$?" > /tmp/m4-status-quote
/usr/bin/m4 <<'M4_INPUT' > /tmp/m4-stack
define(`x', `base')dnl
pushdef(`x', `top')dnl
x
popdef(`x')dnl
x
M4_INPUT
printf '%s\n' "$?" > /tmp/m4-status-stack
/usr/bin/m4 <<'M4_INPUT' > /tmp/m4-eval
eval((6 * 7) + 8 / 2)
eval(255, 16, 4)
ifelse(`a', `a', `equal', `bad')
ifdef(`eval', `builtin', `bad')
format(`%04d:%s:%x', 7, `word', 255)
M4_INPUT
printf '%s\n' "$?" > /tmp/m4-status-eval
/usr/bin/m4 <<'M4_INPUT' > /tmp/m4-divert
divert(1)dnl
second
divert(0)dnl
start
undivert(1)dnl
end
M4_INPUT
printf '%s\n' "$?" > /tmp/m4-status-divert
/usr/bin/m4 <<'M4_INPUT' > /tmp/m4-string
len(`abc def') substr(`abcdef', 2, 3) index(`abcdef', `cd')
translit(`abracadabra', `abc', `ABC')
M4_INPUT
printf '%s\n' "$?" > /tmp/m4-status-string
/usr/bin/m4 <<'M4_INPUT' > /tmp/m4-regex
regexp(`ab123cd', `[0-9]+')
patsubst(`a1b22', `[0-9]+', `X')
M4_INPUT
printf '%s\n' "$?" > /tmp/m4-status-regex
/usr/bin/m4 <<'M4_INPUT' > /tmp/m4-recursion
define(`count', `ifelse($1, 0, `done', `$1 count(decr($1))')')dnl
count(3)
M4_INPUT
printf '%s\n' "$?" > /tmp/m4-status-recursion
/usr/bin/m4 <<'M4_INPUT' > /tmp/m4-process
syscmd(`exit 7')sysval
esyscmd(`printf captured')
sysval
syscmd(`kill -TERM $$')sysval
M4_INPUT
printf '%s\n' "$?" > /tmp/m4-status-process
/usr/bin/m4 <<'M4_INPUT' > /tmp/m4-wrap
m4wrap(`wrapped
')dnl
body
M4_INPUT
printf '%s\n' "$?" > /tmp/m4-status-wrap
printf 'included:VALUE\n' > /tmp/m4-include
printf '%s\n' "include(\`m4-include')dnl" > /tmp/m4-main
/usr/bin/m4 -I /tmp -DVALUE=42 /tmp/m4-main > /tmp/m4-include-result
printf '%s\n' "$?" > /tmp/m4-status-include
/usr/bin/m4 -F /tmp/m4-frozen <<'M4_FREEZE'
define(`stored', `frozen')dnl
M4_FREEZE
printf '%s\n' "$?" > /tmp/m4-status-freeze
printf 'stored\n' | /usr/bin/m4 -R /tmp/m4-frozen > /tmp/m4-reload
printf '%s\n' "$?" > /tmp/m4-status-reload
printf 'm4exit(9)\n' | /usr/bin/m4 > /tmp/m4-exit
printf '%s\n' "$?" > /tmp/m4-status-exit
/usr/bin/m4 <<'M4_MISSING' > /tmp/m4-missing 2> /tmp/m4-error
include(`/tmp/m4-does-not-exist')
M4_MISSING
printf '%s\n' "$?" > /tmp/m4-status-missing
/usr/bin/m4 --version > /tmp/m4-version
printf '%s\n' "$?" > /tmp/m4-status-version
