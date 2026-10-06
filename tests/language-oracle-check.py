#!/usr/bin/env python3
"""Compare every installed official language script with the OCaml oracle.

Use interpreter traces and C parser metadata, not another WAST scanner. Keep
ordinary setup, ordered assertion/action coverage and exclusions independent.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src/html-rt/tools"))
import test_distribution as corpus
import vfs

KINDS = {0: "return", 1: "trap", 2: "exception", 3: "exhaustion",
         4: "invalid", 5: "malformed", 6: "unlinkable"}
TRACE = re.compile(
    r'^-- (?:\[\d+ ms\] )?(?:Asserting (?P<kind>[a-z ]+)\.\.\.|'
    r'(?P<action>Invoking function|Getting global) "(?P<name>.*?)"\.\.\.|'
    r'(?P<setup>Initializing)\.\.\.|(?P<reset>Checking|Registering module .*?)\.\.\.)$',
    re.MULTILINE | re.DOTALL)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=True) + "\n")


def oracle_events(trace):
    events, pending, setups = [], None, 0
    for match in TRACE.finditer(trace):
        if match["kind"]:
            kind = match["kind"]
            base = kind.removesuffix(" custom")
            if base not in KINDS.values():
                raise ValueError("unknown oracle assertion trace: " + kind)
            events.append(dict(kind=kind, func="(module)"))
            pending = events[-1] if base in {"return", "trap", "exception", "exhaustion"} else None
        elif match["action"]:
            event = dict(kind="action", func=match["name"],
                         action="get" if match["action"] == "Getting global" else "invoke")
            if pending is not None:
                pending.update(func=event["func"], action=event["action"])
                pending = None
            else:
                events.append(event)
        else:
            setups += bool(match["setup"])
            pending = None
    return events, setups


def c_events(metadata):
    events = []
    for group in metadata["groups"]:
        assertion = group["module_assertion"]
        if assertion is not None:
            events.append(dict(kind=KINDS[assertion["kind"]], func="(module)"))
        for assertion in group["assertions"]:
            events.append(dict(kind=KINDS[assertion["kind"]], func=assertion["func"],
                               action=assertion["action"]))
    return events


def decoded_name(name):
    """Decode the displayed name only; never frame or parse WAST commands."""
    result = bytearray()
    i = 0
    while i < len(name):
        c = name[i]
        i += 1
        if c != "\\":
            result.extend(c.encode("utf-8"))
            continue
        if i == len(name):
            raise ValueError("unterminated name escape")
        if name[i] == "u" and name[i:i + 2] == "u{":
            end = name.index("}", i + 2)
            result.extend(chr(int(name[i + 2:end], 16)).encode("utf-8"))
            i = end + 1
        elif re.fullmatch("[0-9a-fA-F]{2}", name[i:i + 2]):
            result.append(int(name[i:i + 2], 16))
            i += 2
        else:
            escapes = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\"}
            if name[i] not in escapes:
                raise ValueError("unknown name escape")
            result.extend(escapes[name[i]].encode("utf-8"))
            i += 1
    return result.decode("utf-8")


def decoded_internal_name(name):
    result = bytearray()
    i = 0
    while i < len(name):
        char = name[i]
        i += 1
        if char == "\x01":
            if i == len(name) or name[i] not in "01":
                raise ValueError("malformed C internal name encoding")
            result.append(0 if name[i] == "0" else 1)
            i += 1
        else:
            result.extend(char.encode("utf-8"))
    return result.decode("utf-8")


def normalized(event, *, c_name_internal=False):
    # The C report treats bare actions and void assert_return alike. Custom
    # assertions retain their independent oracle category but use base C kinds.
    kind = event["kind"].removesuffix(" custom")
    if kind == "action":
        kind = "return"
    name = (decoded_internal_name(event["func"]) if c_name_internal
            else decoded_name(event["func"]))
    return kind, name, event.get("action")


def category(event):
    return "instantiation-trap" if event["kind"] == "trap" and "action" not in event else event["kind"]


def mismatch(left, right):
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            return dict(index=index, left=a, right=b)
    if len(left) != len(right):
        index = min(len(left), len(right))
        return dict(index=index, left=left[index:index + 1], right=right[index:index + 1])
    return None


def indexed_report(report, identities, field):
    tests = report["tests"]
    indexed = {t["identity"]: t for t in tests}
    if len(indexed) != len(tests) or list(indexed) != identities:
        raise ValueError("report identities/order differ from installed manifest: " + field)
    return indexed


def successful_report(test, field):
    report = test.get(field, {})
    results = report.get("assertions", report.get("results", []))
    setup = report.get("setup", {})
    return (test["status"] == "PASS" and report.get("completed") is True and
            report.get("passed") == report.get("total") == len(results) and
            all(a.get("pass") is True for a in results) and setup.get("complete") is True and
            setup.get("passed") == setup.get("total") and setup.get("failures") == [])


def expected_gap(record, policy):
    if record["id"] not in policy:
        return False
    return sorted(i["kind"] for i in record["issues"]) == sorted(policy[record["id"]]["issueKinds"])


def run(command, timeout, stdout_path, stderr_path):
    timed_out = False
    with stdout_path.open("wb") as out, stderr_path.open("wb") as err:
        try:
            result = subprocess.run(command, stdout=out, stderr=err, timeout=timeout)
            returncode = result.returncode
        except subprocess.TimeoutExpired:
            returncode, timed_out = None, True
    return dict(command=command, returncode=returncode, timedOut=timed_out,
                stdout=str(stdout_path), stderr=str(stderr_path),
                stdoutSha256=sha(stdout_path.read_bytes()),
                stderrSha256=sha(stderr_path.read_bytes()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-results", type=Path, required=True)
    parser.add_argument("--browser-results", type=Path)
    parser.add_argument("--vfs-root", type=Path, default=REPO / "src/vfs")
    parser.add_argument("--c-runner", type=Path, default=REPO / "build/cli-rt/waste-cli")
    parser.add_argument("--oracle", type=Path, default=REPO / "build/cli-rt/waste-wast-ocaml")
    parser.add_argument("--output", type=Path, default=REPO / "build/engine/language-oracle")
    parser.add_argument("--policy", type=Path, default=REPO / "tests/language-oracle-policy.json")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--strict", action="store_true", help="fail for recorded gaps as well as new gaps")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    root = args.vfs_root.resolve()
    manifest_path = root / "tests/manifest.json"
    manifest = json.loads(manifest_path.read_bytes())
    inventory = vfs.load(root)
    corpus.audit(manifest, lambda p: vfs.local(root, p).read_bytes(), vfs.audit_tree(root, inventory))
    corpus.verify_sources(REPO, manifest)
    identities = [t["id"] for t in manifest["tests"]]
    native = indexed_report(json.loads(args.native_results.read_bytes()), identities, "native")
    browser = indexed_report(json.loads(args.browser_results.read_bytes()), identities, "browser") if args.browser_results else None
    policy = json.loads(args.policy.read_bytes())
    if policy.get("format") != 1:
        raise ValueError("unsupported language comparison policy")
    gaps = {g["id"]: g for g in policy["expectedGaps"]}
    supported = {t["id"]: t for t in manifest["tests"] if t["suite"] == "wasm-spec" and not t["unsupported"]}
    if len(gaps) != len(policy["expectedGaps"]) or set(gaps) - set(supported):
        raise ValueError("duplicate or out-of-scope expected gap")
    for identity, gap in gaps.items():
        if gap["sourceSha256"] != supported[identity]["source"]["sha256"] or not gap["reason"] or not gap["issueKinds"]:
            raise ValueError("stale or unexplained expected gap: " + identity)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for test in manifest["tests"]:
        identity = test["id"]
        record = dict(id=identity, group=test["group"], source=test["source"],
                      requiredFeatures=test["requiredFeatures"], manifestUnsupported=test["unsupported"])
        records.append(record)
        if test["suite"] != "wasm-spec":
            record.update(state="outside-official-scope", reason="Repository fixture; POSIX/libc imports are not an OCaml language acceptance gate")
            continue
        if test["unsupported"]:
            record.update(state="excluded", reason=test["unsupportedReason"])
            continue
        stem = output / identity
        stem.parent.mkdir(parents=True, exist_ok=True)
        source = str(vfs.local(root, test["path"]))
        c_run = run([str(args.c_runner.resolve()), "--browser-spec", source], args.timeout,
                    Path(str(stem) + ".c-metadata.json"), Path(str(stem) + ".c-stderr.txt"))
        oracle_flags = ["-ca", "-t"] if test["group"].startswith("custom/") else ["-t"]
        oracle = run([str(args.oracle.resolve()), *oracle_flags, source], args.timeout,
                     Path(str(stem) + ".ocaml-trace.txt"), Path(str(stem) + ".ocaml-stderr.txt"))
        record.update(cMetadata=c_run, oracle=oracle)
        issues = []
        nr = native[identity].get("nativeReport", {})
        record["native"] = dict(status=native[identity]["status"], passed=nr.get("passed"), total=nr.get("total"),
                                completed=nr.get("completed"), setup=nr.get("setup"))
        if not successful_report(native[identity], "nativeReport"):
            issues.append(dict(kind="native-acceptance", report=record["native"]))
        if browser is not None:
            br = browser[identity].get("browserReport", {})
            same = (browser[identity]["status"] == native[identity]["status"] and
                    br.get("setup") == nr.get("setup") and br.get("completed") == nr.get("completed") and
                    br.get("total") == nr.get("total") and br.get("passed") == nr.get("passed") and
                    [(a["func"], a["pass"]) for a in br.get("results", [])] ==
                    [(a["func"], a["pass"]) for a in nr.get("assertions", [])])
            record["browserResultParity"] = same
            if not same:
                issues.append(dict(kind="native-browser-results"))
        if c_run["returncode"] != 0 or Path(c_run["stderr"]).read_bytes():
            issues.append(dict(kind="metadata-incomplete"))
        if oracle["returncode"] != 0:
            issues.append(dict(kind="oracle-timeout" if oracle["timedOut"] else "oracle-rejection"))
        ce, oe, setups = [], [], None
        try:
            ce = c_events(json.loads(Path(c_run["stdout"]).read_bytes()))
            oe, setups = oracle_events(Path(oracle["stdout"]).read_bytes().decode("utf-8"))
        except (ValueError, KeyError) as error:
            issues.append(dict(kind="comparison-data", error=str(error)))
        trace = Path(oracle["stdout"]).read_bytes().decode("utf-8", errors="replace")
        representations = {name: len(re.findall(r'^-- (?:\[\d+ ms\] )?' + marker + r'\.\.\.$', trace, re.MULTILINE))
                           for name, marker in [("binaryDefinitions", "Decoding"), ("quotedTextDefinitions", "Parsing quote")]}
        record.update(cKinds=dict(Counter(map(category, ce))),
                      oracleKinds=dict(Counter(map(category, oe))), oracleSetups=setups,
                      oracleInputRepresentations=representations)
        difference = mismatch(
            [normalized(event, c_name_internal=True) for event in ce],
            list(map(normalized, oe)))
        if difference:
            issues.append(dict(kind="ordered-language-checks", firstDifference=difference, cCount=len(ce), oracleCount=len(oe)))
        difference = mismatch([e["func"] for e in ce], [a["func"] for a in nr.get("assertions", [])])
        if difference:
            issues.append(dict(kind="metadata-execution-coverage", firstDifference=difference))
        if oracle["returncode"] == 0 and setups != nr.get("setup", {}).get("total"):
            issues.append(dict(kind="ordinary-setup-count", c=nr.get("setup", {}).get("total"), oracle=setups))
        record.update(issues=issues, state="gap" if issues else "agreed")
        record["expectedGap"] = expected_gap(record, gaps)
        if identity in gaps:
            record["gapPolicy"] = gaps[identity]
        if issues:
            print("GAP " + identity + ": " + ", ".join(i["kind"] for i in issues), flush=True)
        elif sum(r["state"] == "agreed" for r in records) % 25 == 0:
            print("Compared " + str(sum(r["state"] == "agreed" for r in records)) + " official inputs", flush=True)
    unexpected = [r["id"] for r in records if r["state"] == "gap" and not r["expectedGap"]]
    repaired = [r["id"] for r in records if r["state"] == "agreed" and r["id"] in gaps]
    summary = dict(Counter(r["state"] for r in records))
    summary.update(officialIdentities=len(supported) + summary.get("excluded", 0), supportedOfficialInputs=len(supported),
                   unexpectedGaps=unexpected, repairedGaps=repaired,
                   allSupportedInputsAccepted=all(r["oracle"]["returncode"] == 0 and
                       not any(i["kind"] == "native-acceptance" for i in r["issues"])
                       for r in records if r["state"] in {"agreed", "gap"}),
                   languageComparisonComplete=not any(r["state"] == "gap" for r in records))
    ledger = dict(format=1, scope="Installed official Wasm/WAT/WAST language scripts; standard spectest scaffolding only",
                  comparisonToolSha256=sha(Path(__file__).read_bytes()),
                  manifestSha256=sha(manifest_path.read_bytes()), selectionSha256=manifest["selection_sha256"],
                  oracleProvenance=manifest["oracle"], oracleProfiles={"default": ["-t"], "custom/*": ["-ca", "-t"]},
                  subprocessTimeoutSeconds=args.timeout,
                  artifacts={str(p.resolve()): sha(p.read_bytes()) for p in [args.c_runner, args.oracle]},
                  reports={str(p.resolve()): sha(p.read_bytes()) for p in [args.native_results, args.browser_results] if p},
                  policySha256=sha(args.policy.read_bytes()), summary=summary, records=records)
    write_json(output / "ledger.json", ledger)
    print(json.dumps(summary, sort_keys=True), flush=True)
    return int(bool(unexpected or repaired or (args.strict and not summary["languageComparisonComplete"])))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, KeyError, OSError) as error:
        print("language comparison configuration error: " + str(error), file=sys.stderr)
        sys.exit(2)
