"""Read the integer configuration shared by C, packaging, and host harnesses.

Keep config.h declarative: single-line integer macros using names, parentheses,
addition, subtraction and multiplication. No compiler or JavaScript eval needed.
"""
import argparse
import ast
import json
from pathlib import Path
import re

CONFIG_PATH = Path(__file__).resolve().parents[3] / "src/config.h"
CONFIGURED_SCRIPTS = {"worker.js", "test-suite.js", "tests-worker.js"}


def read_config(path=CONFIG_PATH):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", Path(path).read_text(), flags=re.DOTALL)
    expressions = {}
    for line in text.splitlines():
        match = re.fullmatch(r"\s*#\s*define\s+(\w+)\s+(.+?)\s*", line)
        if match:
            name, expression = match.groups()
            if name in expressions:
                raise ValueError(f"duplicate {name} in {path}")
            expressions[name] = re.sub(r"(?<=\d)[uUlL]+\b", "", expression)
    values, pending = {}, set()

    def value(name):
        if name in values:
            return values[name]
        if name not in expressions or name in pending:
            raise ValueError(f"unknown or cyclic config name {name} in {path}")
        pending.add(name)
        result = evaluate(ast.parse(expressions[name], mode="eval").body)
        if result <= 0:
            raise ValueError(f"{name} must be positive in {path}")
        pending.remove(name)
        values[name] = result
        return result

    def evaluate(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.Name):
            return value(node.id)
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult)):
            left, right = evaluate(node.left), evaluate(node.right)
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
            return left * right
        raise ValueError(f"unsupported integer expression in {path}")

    for name in expressions:
        value(name)
    return values


def javascript(config=None):
    data = json.dumps(read_config() if config is None else config,
                      sort_keys=True, separators=(",", ":"))
    return '"use strict";\n' + "globalThis.WASTE_CONFIG = Object.freeze(" + data + ");\n"


def frontend_bytes(path):
    path = Path(path)
    data = path.read_bytes()
    return javascript().encode() + data if path.name in CONFIGURED_SCRIPTS else data


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=CONFIG_PATH)
    parser.add_argument("--javascript", action="store_true")
    parser.add_argument("--get", metavar="NAME")
    args = parser.parse_args()
    config = read_config(args.config)
    print(config[args.get] if args.get else javascript(config) if args.javascript else json.dumps(config, sort_keys=True), end="\n")
