#!/usr/bin/env python3
"""Negative controls for the language ledger's independent coverage accounting."""
import copy
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location("language_ledger", Path(__file__).with_name("language-oracle-check.py"))
ledger = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ledger)

trace = '''-- [0 ms] Checking...
-- [1 ms] Initializing...
-- [2 ms] Asserting return...
-- [2 ms] Invoking function "λ"...
-- [2 ms] Asserting invalid...
-- [3 ms] Asserting malformed custom...
-- [3 ms] Asserting trap...
-- [3 ms] Checking...
-- [3 ms] Initializing...
-- [4 ms] Invoking function "bare"...
-- [4 ms] Asserting trap...
-- [4 ms] Invoking function "quoted"name
with-newline"...
-- [5 ms] Asserting return...
-- [5 ms] Getting global "g"...
'''
events, setups = ledger.oracle_events(trace)
assert setups == 2
assert [(e["kind"], e["func"]) for e in events] == [
    ("return", "λ"), ("invalid", "(module)"), ("malformed custom", "(module)"),
    ("trap", "(module)"), ("action", "bare"), ("trap", 'quoted"name\nwith-newline'), ("return", "g")]
assert events[-1]["action"] == "get"
assert ledger.decoded_name(r'\u{feff}\u{00}\"\\') == '\ufeff\x00"\\'
assert ledger.decoded_name(r'\ef\bb\bf\00\"\\') == '\ufeff\x00"\\'
assert ledger.decoded_name(r'\\00') == r'\00', "literal backslashes must not turn into NUL"
assert ledger.decoded_name(r'\00') != ledger.decoded_name(r'\\00')
assert ledger.decoded_name('Å') != ledger.decoded_name('Å'), "do not normalize distinct Unicode export names"
assert ledger.decoded_name(r'\41') == 'A', "byte escapes must decode in WAT names"
assert ledger.decoded_internal_name("\x01" + "0") == "\x00"
assert ledger.decoded_internal_name("\x01" + "1") == "\x01"
assert ledger.decoded_internal_name("\x01" + "0") != ledger.decoded_internal_name("\x01" + "1")
internal = [ledger.normalized(dict(kind="action", func=name, action="invoke"),
                              c_name_internal=True)
            for name in ("\x01" + "0", "\x01" + "1")]
assert internal[0][1] == "\x00" and internal[1][1] == "\x01"
metadata = {"groups": [
    {"module_assertion": None, "assertions": [{"kind": 0, "func": "λ", "action": "invoke"}]},
    {"module_assertion": {"kind": 4}, "assertions": []},
    {"module_assertion": {"kind": 5}, "assertions": []},
    {"module_assertion": {"kind": 1}, "assertions": []},
    {"module_assertion": None, "assertions": [
        {"kind": 0, "func": "bare", "action": "invoke"},
        {"kind": 1, "func": 'quoted"name\nwith-newline', "action": "invoke"},
        {"kind": 0, "func": "g", "action": "get"}]}]}
c = list(map(ledger.normalized, ledger.c_events(metadata)))
o = list(map(ledger.normalized, events))
assert ledger.mismatch(c, o) is None
assert ledger.mismatch(c, o[:-1])["index"] == 6
bad = copy.deepcopy(metadata)
bad["groups"][1]["module_assertion"]["kind"] = 5
assert ledger.mismatch(list(map(ledger.normalized, ledger.c_events(bad))), o)["index"] == 1
assert ledger.mismatch(c, o[1:] + o[:1])["index"] == 0
try:
    ledger.oracle_events("-- Asserting unknown...\n")
except ValueError:
    pass
else:
    raise AssertionError("unknown oracle assertion category was hidden")

test = dict(identity="core/a.wast", status="PASS", nativeReport=dict(
    completed=True, passed=1, total=1, assertions=[dict(func="a", **{"pass": True})],
    setup=dict(total=2, passed=2, complete=True, failures=[])))
assert ledger.successful_report(test, "nativeReport")
for mutation in [
    lambda t: t["nativeReport"].update(completed=False),
    lambda t: t["nativeReport"]["assertions"].clear(),
    lambda t: t["nativeReport"]["assertions"][0].update({"pass": False}),
    lambda t: t["nativeReport"]["setup"].update(passed=1, failures=[dict(line=2)]),
    lambda t: t["nativeReport"]["setup"].update(complete=False),
]:
    bad = copy.deepcopy(test); mutation(bad)
    assert not ledger.successful_report(bad, "nativeReport")
for tests in [[test, test], [dict(test, identity="core/b.wast")], []]:
    try:
        ledger.indexed_report(dict(tests=tests), ["core/a.wast"], "native")
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate/stale/missing report identity accepted")
gap = dict(id="core/a.wast", issues=[dict(kind="oracle-rejection")])
policy = {"core/a.wast": dict(issueKinds=["oracle-rejection"])}
assert ledger.expected_gap(gap, policy)
gap["issues"].append(dict(kind="ordered-language-checks"))
assert not ledger.expected_gap(gap, policy), "new coverage loss was hidden behind an old expected gap"
print("PASS language ledger: ordering, omission, kind distinctions, raw names, setup/EOF failures and stale/expanded gap guards")
