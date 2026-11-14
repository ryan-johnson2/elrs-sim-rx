"""Minimal KiCad S-expression reader/writer shared by the generator scripts."""


class Q(str):
    """A token that was (or must be) written in double quotes."""


def parse(text):
    pos = 0
    n = len(text)
    stack = [[]]
    while pos < n:
        c = text[pos]
        if c in " \t\r\n":
            pos += 1
        elif c == "(":
            new = []
            stack[-1].append(new)
            stack.append(new)
            pos += 1
        elif c == ")":
            stack.pop()
            pos += 1
        elif c == '"':
            pos += 1
            out = []
            while text[pos] != '"':
                if text[pos] == "\\":
                    out.append(text[pos:pos + 2])
                    pos += 2
                else:
                    out.append(text[pos])
                    pos += 1
            pos += 1
            stack[-1].append(Q("".join(out)))
        else:
            start = pos
            while pos < n and text[pos] not in ' \t\r\n()"':
                pos += 1
            stack[-1].append(text[start:pos])
    return stack[0][0]


def dump(node, indent=0):
    if isinstance(node, Q):
        return '"%s"' % node
    if isinstance(node, str):
        return node
    flat = "(" + " ".join(dump(c) for c in node) + ")"
    if len(flat) <= 100 and "\n" not in flat:
        return flat
    pad = "\t" * (indent + 1)
    head = [dump(node[0])]
    rest = node[1:]
    while rest and not isinstance(rest[0], list):
        head.append(dump(rest[0]))
        rest = rest[1:]
    lines = ["(" + " ".join(head)]
    lines += [pad + dump(c, indent + 1) for c in rest]
    lines.append("\t" * indent + ")")
    return "\n".join(lines)


def find(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def first(node, key):
    found = find(node, key)
    return found[0] if found else None
