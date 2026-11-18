"""Small Molang checker (M1): catches what makes Bedrock drop a whole animation/controller.

Not a full Molang implementation: it tokenizes and checks the expression grammar (operands and operators
alternate, brackets balance, ternaries pair up, statements end cleanly). ModPorter E-023 found that a single
"q.anim_time * 10 5" kills every animation in the file, so every generated expression goes through check().
"""
import re

TOKEN = re.compile(r"""\s*(?:
    (?P<num>\d+\.?\d*f?|\.\d+f?)|
    (?P<str>'[^']*')|
    (?P<id>[A-Za-z_][\w]*(?:\.[A-Za-z_][\w]*)*)|
    (?P<op>\?\?|==|!=|<=|>=|&&|\|\||->|[-+*/<>!?:=;,()\[\]{}])
)""", re.X)
PREFIXES = ("q", "query", "v", "variable", "t", "temp", "c", "context", "math", "geometry", "material", "texture", "array")
KEYWORDS = {"return", "loop", "for_each", "break", "continue", "this", "true", "false"}


def tokens(expr):
    pos, out = 0, []
    while pos < len(expr):
        if expr[pos:].strip() == "": break
        m = TOKEN.match(expr, pos)
        if not m or m.end() == pos: raise ValueError(f"bad character at {pos}: {expr[pos:pos + 12]!r}")
        kind = m.lastgroup; out.append((kind, m.group(kind))); pos = m.end()
    return out


def check(expr):
    """None if fine, else a short reason."""
    if isinstance(expr, (int, float)): return None
    try: toks = tokens(str(expr))
    except ValueError as e: return str(e)
    if not toks: return "empty"
    depth, expect_operand, ternary = [], True, 0
    for kind, val in toks:
        if kind == "id" and "." in val and val.split(".")[0].lower() not in PREFIXES:
            return f"unknown prefix in {val}"
        if kind == "id" and "." not in val and val.lower() not in KEYWORDS:
            return f"bare name {val}"
        if kind in ("num", "str", "id"):
            if not expect_operand and val.lower() not in KEYWORDS: return f"two operands in a row near {val!r}"
            expect_operand = val.lower() in ("return",)
            continue
        if val in ("(", "[", "{"):
            if not expect_operand and val == "(":
                pass   # function call: math.sin(...)
            elif not expect_operand and val == "[":
                pass   # array index
            depth.append(val); expect_operand = val != "{"; continue
        if val in (")", "]", "}"):
            if not depth or {"(": ")", "[": "]", "{": "}"}[depth.pop()] != val: return f"unbalanced {val}"
            expect_operand = False; continue
        if val == "!":
            if not expect_operand: return "! after an operand"
            continue
        if val == "-" and expect_operand: continue      # unary minus
        if val == ";": expect_operand = True; continue
        if val == ",": expect_operand = True; continue
        if val == "?": ternary += 1; expect_operand = True; continue
        if val == ":":
            ternary -= 1
            if ternary < 0: return "':' without '?'"
            expect_operand = True; continue
        if expect_operand: return f"operator {val!r} where a value was expected"
        expect_operand = True
    if depth: return "unclosed bracket"
    if expect_operand and toks[-1][1] not in (";", "}"): return "ends with an operator"
    return None
