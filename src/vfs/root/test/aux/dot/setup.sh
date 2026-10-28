# Prepare observations; installed.wast owns every assertion.
LC_ALL=C
export LC_ALL
cat > /tmp/dot-input <<'GRAPH'
digraph sample {
  node [shape=box, style=filled, fillcolor=red, width=1, height=0.5, fixedsize=true];
  a [label="one & two"];
  b [label="next"];
  a -> b [label="link"];
}
GRAPH
/usr/bin/dot -Tsvg /tmp/dot-input > /tmp/dot-svg 2> /tmp/dot-svg-error
printf '%s\n' "$?" > /tmp/dot-svg-status
/usr/bin/dot -Tplain /tmp/dot-input > /tmp/dot-plain
printf '%s\n' "$?" > /tmp/dot-plain-status
/usr/bin/dot -Grankdir=LR -Tplain /tmp/dot-input > /tmp/dot-lr
printf '%s\n' "$?" > /tmp/dot-lr-status
/usr/bin/dot -Tcanon /tmp/dot-input > /tmp/dot-canon
printf '%s\n' "$?" > /tmp/dot-canon-status
/usr/bin/dot -Tdot /tmp/dot-input > /tmp/dot-layout
printf '%s\n' "$?" > /tmp/dot-layout-status
/usr/bin/dot -Tjson /tmp/dot-input > /tmp/dot-json
printf '%s\n' "$?" > /tmp/dot-json-status
/usr/bin/dot -Txdot /tmp/dot-input > /tmp/dot-xdot
printf '%s\n' "$?" > /tmp/dot-xdot-status
/usr/bin/dot -Tsvg -o /tmp/dot-output /tmp/dot-input > /tmp/dot-output-stdout
printf '%s\n' "$?" > /tmp/dot-output-status
/usr/bin/dot -Tsvg <<'GRAPH' > /tmp/dot-cluster
digraph grouped { subgraph cluster_outer { label="group"; x; y; x -> y; } z; y -> z; }
GRAPH
printf '%s\n' "$?" > /tmp/dot-cluster-status
printf 'graph undirected { left -- right }\n' | /usr/bin/dot -Tplain > /tmp/dot-undirected
printf '%s\n' "$?" > /tmp/dot-undirected-status
printf 'digraph first { a }\ndigraph second { b }\n' | /usr/bin/dot -Tsvg > /tmp/dot-multiple
printf '%s\n' "$?" > /tmp/dot-multiple-status
printf 'digraph broken { a -> }\n' | /usr/bin/dot -Tsvg > /tmp/dot-invalid 2> /tmp/dot-invalid-error
printf '%s\n' "$?" > /tmp/dot-invalid-status
/usr/bin/dot -Tnot-a-format /tmp/dot-input > /tmp/dot-format 2> /tmp/dot-format-error
printf '%s\n' "$?" > /tmp/dot-format-status
/usr/bin/dot -Tsvg /tmp/dot-missing-input > /tmp/dot-missing 2> /tmp/dot-missing-error
printf '%s\n' "$?" > /tmp/dot-missing-status
/usr/bin/dot -V > /tmp/dot-version-out 2> /tmp/dot-version
printf '%s\n' "$?" > /tmp/dot-version-status
