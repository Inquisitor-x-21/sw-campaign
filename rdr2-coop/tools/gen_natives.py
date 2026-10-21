#!/usr/bin/env python3
"""Generate plugin/natives_gen.h from plugin/natives.list.

Every hash, parameter list and return type comes from the community RDR2
native database (https://github.com/alloc8or/rdr3-nativedb-data), so nothing
is typed by hand. Run it again whenever you add a name to natives.list:

    python tools/gen_natives.py --fetch            # downloads natives.json
    python tools/gen_natives.py --db natives.json  # uses a local copy
"""
import argparse
import json
import os
import re
import sys
import urllib.request

DB_URL = "https://raw.githubusercontent.com/alloc8or/rdr3-nativedb-data/master/natives.json"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Database type -> C++ type used in the generated wrappers.
TYPE_MAP = {
    "void": "void",
    "BOOL": "BOOL",
    "int": "int",
    "float": "float",
    "Any": "Any",
    "Hash": "Hash",
    "Ped": "Ped",
    "Entity": "Entity",
    "Player": "Player",
    "Vehicle": "Vehicle",
    "Object": "Object",
    "Blip": "Blip",
    "Vector3": "NVec3",
    "const char*": "const char*",
    "char*": "char*",
}

# Parameter names that collide with C++ keywords or Windows header macros.
RESERVED = {
    "default", "class", "new", "delete", "operator", "this", "register",
    "template", "union", "unsigned", "signed", "char", "int", "float", "bool",
    "double", "long", "short", "void", "auto", "case", "switch", "const",
    "struct", "public", "private", "protected", "friend", "virtual", "near",
    "far", "min", "max", "IN", "OUT", "OPTIONAL", "ERROR", "DELETE", "small",
    "interface", "pascal", "cdecl", "CALLBACK", "VOID", "CONST", "TRUE",
    "FALSE", "BOOL", "Hash", "Ped", "Entity", "Player", "Vehicle", "Object",
    "Blip", "Any",
}


def map_type(t):
    t = t.strip()
    if t in TYPE_MAP:
        return TYPE_MAP[t]
    if t.endswith("*"):
        base = t[:-1].strip()
        if base in TYPE_MAP and base not in ("void", "Vector3"):
            return TYPE_MAP[base] + "*"
    raise ValueError(f"unsupported type '{t}'")


def safe_name(n, used):
    n = re.sub(r"[^A-Za-z0-9_]", "_", n) or "p"
    if n[0].isdigit():
        n = "p" + n
    if n in RESERVED:
        n += "_"
    base, i = n, 2
    while n in used:
        n = f"{base}{i}"
        i += 1
    used.add(n)
    return n


def load_db(args):
    if args.db:
        with open(args.db, "r", encoding="utf-8") as f:
            return json.load(f)
    if not args.fetch:
        sys.exit("pass --db natives.json or --fetch")
    print(f"downloading {DB_URL}")
    with urllib.request.urlopen(DB_URL, timeout=60) as r:
        return json.loads(r.read().decode("utf-8"))


def parse_list(path):
    entries = []
    with open(path, "r", encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            m = re.fullmatch(r"([A-Z0-9_]+)::([A-Z0-9_]+)", parts[0])
            if not m:
                sys.exit(f"{path}:{lineno}: expected NAMESPACE::NAME, got '{parts[0]}'")
            entries.append((m.group(1), m.group(2), "!hash" in parts[1:], lineno))
    return entries


def comment_lines(text):
    text = (text or "").strip()
    if not text:
        return []
    out = []
    for para in text.splitlines():
        para = para.strip()
        if para:
            out.append("    /// " + para.replace("*/", "* /"))
    return out[:6]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", help="path to natives.json")
    ap.add_argument("--fetch", action="store_true", help="download natives.json")
    ap.add_argument("--list", default=os.path.join(ROOT, "plugin", "natives.list"))
    ap.add_argument("--out", default=os.path.join(ROOT, "plugin", "natives_gen.h"))
    args = ap.parse_args()

    db = load_db(args)
    entries = parse_list(args.list)

    by_ns = {}
    errors = []
    for ns, name, hash_only, lineno in entries:
        table = db.get(ns)
        if table is None:
            errors.append(f"line {lineno}: namespace {ns} not in database")
            continue
        hits = [(h, v) for h, v in table.items() if v["name"] == name]
        if len(hits) != 1:
            errors.append(f"line {lineno}: {ns}::{name} found {len(hits)} times in database")
            continue
        h, v = hits[0]
        params = v["params"]
        variadic = any(p["type"] == "..." or p["name"] == "..." for p in params)
        if variadic and not hash_only:
            errors.append(f"line {lineno}: {ns}::{name} is variadic; mark it !hash and wrap it by hand")
            continue
        try:
            ret = map_type(v["return_type"])
            ptypes = [] if hash_only else [map_type(p["type"]) for p in params]
        except ValueError as e:
            errors.append(f"line {lineno}: {ns}::{name}: {e}")
            continue
        by_ns.setdefault(ns, []).append((name, h, v, ret, ptypes, hash_only))

    if errors:
        print("\n".join(errors), file=sys.stderr)
        sys.exit(1)

    out = [
        "// GENERATED FILE - do not edit by hand.",
        "// Source: plugin/natives.list + community RDR2 native database",
        "// (https://github.com/alloc8or/rdr3-nativedb-data). Regenerate with tools/gen_natives.py.",
        "#pragma once",
        '#include "native_invoker.h"',
        "",
        "namespace rdr {",
    ]
    count = 0
    for ns in sorted(by_ns):
        out.append(f"namespace {ns} {{")
        for name, h, v, ret, ptypes, hash_only in by_ns[ns]:
            hval = int(h, 16)
            out.append(f"    constexpr uint64_t {name}_HASH = 0x{hval:016X}ull;")
            if hash_only:
                count += 1
                continue
            out.extend(comment_lines(v.get("comment")))
            used = set()
            names = [safe_name(p["name"], used) for p in v["params"]]
            sig = ", ".join(f"{t} {n}" for t, n in zip(ptypes, names))
            call_args = "".join(f", {n}" for n in names)
            out.append(f"    inline {ret} {name}({sig}) {{ return invoke<{ret}>({name}_HASH{call_args}); }}")
            count += 1
        out.append(f"}} // namespace {ns}")
        out.append("")
    out.append("} // namespace rdr")
    out.append("")

    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print(f"wrote {count} natives to {os.path.relpath(args.out, ROOT)}")


if __name__ == "__main__":
    main()
