import json
import sys


def _inline(obj):
    if isinstance(obj, dict):
        if not obj:
            return "{}"
        items = [f"{json.dumps(k)}: {_inline(v)}" for k, v in obj.items()]
        return "{" + ", ".join(items) + "}"
    if isinstance(obj, list):
        if not obj:
            return "[]"
        items = [_inline(item) for item in obj]
        return "[" + ", ".join(items) + "]"
    return json.dumps(obj)


def _pretty(obj, indent=0, expand_all=False):
    pad = "  " * indent
    if isinstance(obj, dict):
        if not obj:
            return "{}"
        if len(obj) == 1 and not expand_all:
            return _inline(obj)
        items = []
        for k, v in obj.items():
            child_expand = expand_all or k in ("bounds", "strategy")
            items.append(f"{pad}  {json.dumps(k)}: {_pretty(v, indent + 1, child_expand)}")
        return "{\n" + ",\n".join(items) + f"\n{pad}}}"
    if isinstance(obj, list):
        if not obj:
            return "[]"
        items = [_inline(item) for item in obj]
        if any(isinstance(item, (dict, list)) for item in obj):
            return "[\n" + f"{pad}  " + f",\n{pad}  ".join(items) + f"\n{pad}]"
        return "[" + ", ".join(items) + "]"
    return json.dumps(obj)


def main():
    if len(sys.argv) < 2:
        print("Usage: python json_collapse_arrays.py <file.json>", file=sys.stderr)
        sys.exit(1)
    path = sys.argv[1]
    with open(path) as f:
        data = json.load(f)
    result = _pretty(data)
    with open(path, "w") as f:
        f.write(result)
        f.write("\n")
    print(f"Formatted {path}")


if __name__ == "__main__":
    main()
