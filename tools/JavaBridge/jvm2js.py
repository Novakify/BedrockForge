"""JavaBridge: JVM bytecode (javap -c -p -s -v text) -> JavaScript for the Bedrock Script API.

Each Java method becomes a JS function with an operand stack and a pc switch (a direct, mechanical translation:
no decompilation guesses). Calls into the mod's own classes stay JS-to-JS; everything else (Minecraft, Forge, the
JDK) goes through the runtime shim `R` (javabridge_runtime.js) by Mojang name (SRG names are mapped at build time
via ModelCore.srg). Original Bedrock Forge code; the mod's code is converted for the user's private use only.
"""
from __future__ import annotations
import json, re, subprocess, os, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from ModelCore.srg import srg_name, srg_owner   # noqa: E402

HDR = re.compile(r"^  (?:[\w$.<>\[\], ?&]+ )?([\w$<>]+|[\w.$]+)\((.*)\)(?: throws [\w.$, ]+)?;$")
INSN = re.compile(r"^\s+(\d+): (\w+)\s*(.*)$")


def javap(classdir, classes):
    env = dict(os.environ, JAVA_TOOL_OPTIONS="")
    r = subprocess.run(["javap", "-c", "-p", "-s", "-v", "-cp", str(classdir)] + classes, capture_output=True, text=True, env=env)
    return r.stdout


def split_classes(text):
    """javap output for many classes -> {class: text}"""
    out, cur, buf = {}, None, []
    for line in text.splitlines():
        if line.startswith("Classfile "):
            if cur: out[cur] = "\n".join(buf)
            cur, buf = None, [line]; continue
        m = re.match(r"^(?:public |private |protected |final |abstract |static |synchronized )*(?:class|interface|enum) ([\w.$]+)", line)
        if m and cur is None: cur = m.group(1).replace(".", "/")
        buf.append(line)
    if cur: out[cur] = "\n".join(buf)
    return out


def comment(rest): return rest.split("//", 1)[1].strip() if "//" in rest else ""


def member(rest):
    c = comment(rest)
    m = re.search(r"(?:Method|Field|InterfaceMethod)\s+(?:([\w/$]+)\.)?\"?([\w$<>]+)\"?:(\S+)", c)
    return (m.group(1) or "", m.group(2), m.group(3)) if m else ("", "", "")


def arg_types(desc):
    inner = desc[desc.index("(") + 1:desc.index(")")]; out, i = [], 0
    while i < len(inner):
        j = i
        while inner[j] == "[": j += 1
        if inner[j] == "L": j = inner.index(";", j)
        out.append(inner[i:j + 1]); i = j + 1
    return out, desc[desc.index(")") + 1:]


def jsstr(s): return json.dumps(s)


_ESC = {"n": "\n", "t": "\t", "r": "\r", "b": "\b", "f": "\f", "'": "'", '"': '"', "\\": "\\"}


def junesc(s):
    """javap prints constants Java-escaped (\\u0001 concat holes, \\' quotes): turn them back into the real string."""
    return re.sub(r"\\(u[0-9a-fA-F]{4}|.)", lambda m: chr(int(m.group(1)[1:], 16)) if m.group(1)[0] == "u" and len(m.group(1)) == 5 else _ESC.get(m.group(1), m.group(1)), s)


class ClassInfo:
    def __init__(self, name, text):
        self.name = name; self.methods = {}; self.boot = {}; self.concat = {}; self.super = None; self.fields = []
        m = re.search(r"(?:class|interface) [\w.$]+(?:<.*?>)? extends ([\w.$]+)", text)
        if m: self.super = m.group(1).replace(".", "/")
        self.parse(text)

    def parse(self, text):
        lines = text.splitlines(); i = 0; cur = None; in_code = False; in_bsm = False; bidx = None
        while i < len(lines):
            line = lines[i]
            if line.startswith("BootstrapMethods:"): in_bsm = True; i += 1; continue
            if in_bsm:
                m = re.match(r"^\s+(\d+): #\d+ REF_invokeStatic (\S+)", line)
                if m: bidx = int(m.group(1)); self.boot[bidx] = {"kind": "concat" if "StringConcatFactory" in m.group(2) else "lambda" if "LambdaMetafactory" in m.group(2) else "other"}
                elif bidx is not None:
                    mm = re.search(r"REF_(invoke\w+|newInvokeSpecial) ([\w/$]+)\.\"?([\w$<>]+)\"?:(\([^ ]*\)\S+)", line)
                    if mm and self.boot[bidx]["kind"] == "lambda" and "target" not in self.boot[bidx]:
                        self.boot[bidx].update(target=(mm.group(2), mm.group(3), mm.group(4)), ref=mm.group(1))
                    mc = re.match(r"^\s+#\d+ (.*)$", line)
                    if mc and self.boot[bidx]["kind"] == "concat" and "recipe" not in self.boot[bidx]:
                        self.boot[bidx]["recipe"] = junesc(mc.group(1))
                i += 1; continue
            if line.startswith("  ") and not line.startswith("   ") and line.rstrip().endswith(";") and "(" in line:
                name_m = re.search(r"([\w$<>.]+)\(", line)
                desc_line = lines[i + 1].strip() if i + 1 < len(lines) else ""
                desc = desc_line.split("descriptor: ", 1)[1] if desc_line.startswith("descriptor:") else None
                name = name_m.group(1) if name_m else "?"
                if "." in name: name = "<init>"
                if line.strip() == "static {};": name = "<clinit>"; desc = "()V"
                if desc:
                    cur = {"name": name, "desc": desc, "static": " static " in f" {line.strip()} ", "code": [], "switch": {}}
                    self.methods[name + desc] = cur
                in_code = False; i += 1; continue
            if line.strip().startswith("static {};"):
                cur = {"name": "<clinit>", "desc": "()V", "static": True, "code": [], "switch": {}}
                self.methods["<clinit>()V"] = cur; i += 1; continue
            if line.strip() == "Code:": in_code = True; i += 1; continue
            if cur is not None and in_code:
                m = INSN.match(line)
                if m:
                    off, op, rest = int(m.group(1)), m.group(2), m.group(3)
                    if op in ("tableswitch", "lookupswitch"):
                        cases, dflt = {}, None; i += 1
                        while i < len(lines) and lines[i].strip() != "}":
                            mm = re.match(r"^\s+(-?\d+|default): (\d+)", lines[i])
                            if mm:
                                if mm.group(1) == "default": dflt = int(mm.group(2))
                                else: cases[int(mm.group(1))] = int(mm.group(2))
                            i += 1
                        cur["code"].append((off, op, rest)); cur["switch"][off] = (cases, dflt)
                    else:
                        cur["code"].append((off, op, rest))
                elif line.strip().startswith(("LineNumberTable", "LocalVariableTable", "Exception table", "StackMapTable", "RuntimeVisible", "Signature", "MethodParameters")):
                    in_code = False
            i += 1


class Transpiler:
    """mod_prefixes: internal-name prefixes of the mod's own classes (transpiled); everything else -> runtime R."""
    def __init__(self, mod_prefixes):
        self.mod = tuple(mod_prefixes); self.classes = {}; self.out = []; self.unknown = {}; self.api = {}

    def is_mod(self, owner): return owner.startswith(self.mod)

    def add(self, info): self.classes[info.name] = info

    def fid(self, owner, name, desc): return f"{owner}.{name}{desc}"

    def resolve_virtual(self, owner, name, desc):
        """Mod method lookup walking mod superclasses."""
        c = owner
        while c and c in self.classes:
            if name + desc in self.classes[c].methods: return self.fid(c, name, desc)
            c = self.classes[c].super
        return None

    def api_name(self, owner, name):
        n = srg_name(name) if re.match(r"[mf]_\d+_", name) else name
        self.api[(owner.split("/")[-1], n)] = self.api.get((owner.split("/")[-1], n), 0) + 1
        return n

    # ------------------------------------------------------------------ method
    def method(self, cls, m):
        code = m["code"]
        if not code: return None
        args, ret = arg_types(m["desc"])
        params = (["self"] if not m["static"] else []) + [f"a{i}" for i in range(len(args))]
        # locals: long/double take two slots
        init, slot = [], 0
        if not m["static"]: init.append(f"L[0]=self;"); slot = 1
        for i, t in enumerate(args):
            init.append(f"L[{slot}]=a{i};"); slot += 2 if t in ("J", "D") else 1
        targets = {0}
        for off, op, rest in code:
            if op.startswith("if") or op in ("goto", "goto_w"):
                targets.add(int(rest.split()[0]))
            if op in ("tableswitch", "lookupswitch"):
                cases, d = m["switch"][off]; targets |= set(cases.values()); targets.add(d)
        body = []
        for off, op, rest in code:
            if off in targets: body.append(f"case {off}:")
            body.append("  " + self.insn(cls, m, off, op, rest))
        fn = (f"function({', '.join(params)}){{const L=[];const S=[];let pc=0,t1,t2,t3,G=0;{''.join(init)}\n"
              f"for(;;){{if(++G>200000)throw new Error('loop guard');switch(pc){{\n" + "\n".join(body) + "\ndefault:throw new Error('bad pc '+pc);}}}")
        return fn

    def insn(self, cls, m, off, op, rest):
        c = comment(rest)
        if op in ("getstatic", "putstatic", "getfield", "putfield") or op.startswith("invoke"):
            # javap leaves the owner out for members of the class itself ("// Method execute:(...)V")
            if re.match(r"(Method|Field|InterfaceMethod)\s+\"?[\w$<>]+\"?:", c):
                rest = re.sub(r"// (Method|Field|InterfaceMethod)\s+", lambda mm: f"// {mm.group(1)} {cls}.", rest, count=1); c = comment(rest)
        P, PU = "S.pop()", "S.push"
        def jump(cond): return f"if({cond}){{pc={int(rest.split()[0])};continue;}}"
        if op in ("nop",): return ""
        if op == "aconst_null": return "S.push(null);"
        if op.startswith(("iconst_", "lconst_", "fconst_", "dconst_")): return f"S.push({op.split('_')[1].replace('m1', '-1')});"
        if op in ("bipush", "sipush"): return f"S.push({int(rest.split()[0])});"
        if op in ("ldc", "ldc_w", "ldc2_w"):
            mm = re.match(r"String (.*)$", c)
            if mm: return f"S.push({jsstr(junesc(mm.group(1)))});"
            mm = re.match(r"(?:int|long|float|double) (\S+?)[dfl]?$", c)
            if mm: return f"S.push({float(mm.group(1)) if not mm.group(1).lstrip('-').isdigit() else int(mm.group(1))});".replace("inf", "Infinity")
            mm = re.match(r"class (\S+)$", c)
            if mm: return f"S.push(R.cls({jsstr(mm.group(1).strip(chr(34)))}));"
            return "S.push(null);"
        mm = re.match(r"([ilfda])load(?:_(\d))?$", op)
        if mm: return f"S.push(L[{mm.group(2) if mm.group(2) is not None else rest.split()[0]}]);"
        mm = re.match(r"([ilfda])store(?:_(\d))?$", op)
        if mm: return f"L[{mm.group(2) if mm.group(2) is not None else rest.split()[0]}]=S.pop();"
        if op == "iinc": a, b = rest.split()[:2]; return f"L[{a.rstrip(',')}]=(L[{a.rstrip(',')}]+{b})|0;"
        if op in ("iaload", "laload", "faload", "daload", "aaload", "baload", "caload", "saload"): return "t1=S.pop();t2=S.pop();S.push(t2[t1]);"
        if op in ("iastore", "lastore", "fastore", "dastore", "aastore", "bastore", "castore", "sastore"): return "t1=S.pop();t2=S.pop();t3=S.pop();t3[t2]=t1;"
        if op == "arraylength": return "S.push(S.pop().length);"
        if op in ("newarray", "anewarray"): return "t1=S.pop();S.push(new Array(t1).fill(" + ("null" if op == "anewarray" else "0") + "));"
        if op == "pop": return "S.pop();"
        if op == "pop2": return "S.pop();"   # long/double are one JS value
        if op == "dup": return "S.push(S[S.length-1]);"
        if op == "dup_x1": return "t1=S.pop();t2=S.pop();S.push(t1,t2,t1);"
        if op == "dup_x2": return "t1=S.pop();t2=S.pop();t3=S.pop();S.push(t1,t3,t2,t1);"
        if op == "dup2": return "S.push(S[S.length-1]);"       # only used on long/double here
        if op == "swap": return "t1=S.pop();t2=S.pop();S.push(t1,t2);"
        ar = {"add": "+", "sub": "-", "mul": "*", "div": "/", "rem": "%"}
        mm = re.match(r"([ilfd])(add|sub|mul|div|rem)$", op)
        if mm:
            o = ar[mm.group(2)]
            if mm.group(1) == "i": return f"t1=S.pop();t2=S.pop();S.push(R.idiv(t2,t1,'{o}'));" if o in "/%" else f"t1=S.pop();t2=S.pop();S.push((t2{o}t1)|0);"
            if mm.group(1) == "l": return f"t1=S.pop();t2=S.pop();S.push(R.ldiv(t2,t1,'{o}'));" if o in "/%" else f"t1=S.pop();t2=S.pop();S.push(t2{o}t1);"
            if mm.group(1) == "f": return f"t1=S.pop();t2=S.pop();S.push(Math.fround(t2{o}t1));"
            return f"t1=S.pop();t2=S.pop();S.push(t2{o}t1);"
        if op in ("ineg", "lneg", "fneg", "dneg"): return "S.push(-S.pop());"
        if op in ("ishl", "lshl"): return "t1=S.pop();t2=S.pop();S.push(t2<<t1);"
        if op in ("ishr", "lshr"): return "t1=S.pop();t2=S.pop();S.push(t2>>t1);"
        if op in ("iushr", "lushr"): return "t1=S.pop();t2=S.pop();S.push(t2>>>t1);"
        if op in ("iand", "land"): return "t1=S.pop();t2=S.pop();S.push(t2&t1);"
        if op in ("ior", "lor"): return "t1=S.pop();t2=S.pop();S.push(t2|t1);"
        if op in ("ixor", "lxor"): return "t1=S.pop();t2=S.pop();S.push(t2^t1);"
        if op in ("i2l", "i2f", "i2d", "l2f", "l2d", "f2d", "d2f", "i2c", "i2s", "i2b", "l2i"): return "" if op not in ("d2f", "l2i") else ("S.push(Math.fround(S.pop()));" if op == "d2f" else "S.push(S.pop()|0);")
        if op in ("f2i", "d2i", "f2l", "d2l"): return "S.push(R.toInt(S.pop()));"
        if op in ("lcmp",): return "t1=S.pop();t2=S.pop();S.push(t2>t1?1:t2<t1?-1:0);"
        if op in ("fcmpl", "dcmpl"): return "t1=S.pop();t2=S.pop();S.push(t2>t1?1:t2<t1?-1:t2===t1?0:-1);"
        if op in ("fcmpg", "dcmpg"): return "t1=S.pop();t2=S.pop();S.push(t2>t1?1:t2<t1?-1:t2===t1?0:1);"
        cmp1 = {"ifeq": "===0", "ifne": "!==0", "iflt": "<0", "ifge": ">=0", "ifgt": ">0", "ifle": "<=0"}
        if op in cmp1: return jump(f"R.num(S.pop()){cmp1[op]}")
        cmp2 = {"if_icmpeq": "===", "if_icmpne": "!==", "if_icmplt": "<", "if_icmpge": ">=", "if_icmpgt": ">", "if_icmple": "<="}
        if op in cmp2: return "t1=S.pop();t2=S.pop();" + jump(f"t2{cmp2[op]}t1")
        if op == "if_acmpeq": return "t1=S.pop();t2=S.pop();" + jump("R.same(t2,t1)")
        if op == "if_acmpne": return "t1=S.pop();t2=S.pop();" + jump("!R.same(t2,t1)")
        if op == "ifnull": return jump("S.pop()==null")
        if op == "ifnonnull": return jump("S.pop()!=null")
        if op in ("goto", "goto_w"): return f"pc={int(rest.split()[0])};continue;"
        if op in ("tableswitch", "lookupswitch"):
            cases, d = m["switch"][off]
            return "t1=S.pop();pc=({" + ",".join(f'{k}:{v}' for k, v in cases.items()) + f"}})[t1]??{d};continue;"
        if op in ("ireturn", "lreturn", "freturn", "dreturn", "areturn"): return "return S.pop();"
        if op == "return": return "return;"
        if op == "athrow": return "throw S.pop();"
        if op in ("monitorenter", "monitorexit"): return "S.pop();"
        if op == "checkcast": return ""
        if op == "instanceof":
            cl = c.replace("class ", "").strip('"'); return f"S.push(R.instOf(S.pop(),{jsstr(cl)})?1:0);"
        if op == "new":
            cl = c.replace("class ", "").strip('"')
            return f"S.push({{__new:{jsstr(cl)}}});" if not self.is_mod(cl) else f"S.push(R.newObj({jsstr(cl)}));"
        if op in ("getstatic", "putstatic", "getfield", "putfield"):
            own, nm, desc = member(rest)
            n = self.api_name(own, nm) if not self.is_mod(own) or re.match(r"f_\d+_$", nm) else nm   # inherited vanilla field (Mob.moveControl) read through a mod class
            if op == "getstatic":
                return f"S.push(M.sget({jsstr(own)},{jsstr(nm)}));" if self.is_mod(own) else f"S.push(R.sget({jsstr(own)},{jsstr(n)}));"
            if op == "putstatic": return f"M.sput({jsstr(own)},{jsstr(nm)},S.pop());"
            if op == "getfield": return f"t1=S.pop();S.push(R.gfield(t1,{jsstr(n)}));"
            return f"t1=S.pop();t2=S.pop();R.pfield(t2,{jsstr(n)},t1);"
        if op.startswith("invoke"):
            if op == "invokedynamic": return self.indy(cls, rest)
            own, nm, desc = member(rest)
            args, ret = arg_types(desc); n = len(args)
            pops = "".join(f"const v{i}=S.pop();" for i in reversed(range(n)))
            alist = ",".join(f"v{i}" for i in range(n))
            push = "" if ret == "V" else "S.push(r);"
            if op == "invokestatic":
                if self.is_mod(own):
                    f = self.resolve_virtual(own, nm, desc) or self.fid(own, nm, desc)
                    return f"{{{pops}const r=@@{f}@@({alist});{push}}}"
                return f"{{{pops}const r=R.st({jsstr(own)},{jsstr(self.api_name(own, nm))},{jsstr(desc)},[{alist}]);{push}}}"
            # instance call: receiver below args
            if nm == "<init>":
                if self.is_mod(own):
                    f = self.fid(own, nm, desc)
                    return f"{{{pops}const o=S.pop();@@{f}@@(o{',' if n else ''}{alist});}}"
                return f"{{{pops}const o=S.pop();const nv=R.make({jsstr(own)},{jsstr(desc)},[{alist}]);R.swap(S,L,o,nv);}}"
            if op == "invokespecial" and self.is_mod(own):
                f = self.resolve_virtual(own, nm, desc) or self.fid(own, nm, desc)
                return f"{{{pops}const o=S.pop();const r=@@{f}@@(o{',' if n else ''}{alist});{push}}}"
            if op == "invokespecial" and not self.is_mod(own):   # super.x(): Java engine behaviour we don't re-run
                return f"{{{pops}const o=S.pop();const r=R.superCall(o,{jsstr(self.api_name(own, nm))},[{alist}]);{push}}}"
            mod_impl = self.resolve_virtual(own, nm, desc) if self.is_mod(own) else None
            api = self.api_name(own, nm)
            return f"{{{pops}const o=S.pop();const r=M.vcall(o,{jsstr(nm + desc)},{jsstr(api)},[{alist}],{jsstr(own)});{push}}}"
        self.unknown[op] = self.unknown.get(op, 0) + 1
        return f"/* {op} {rest} */"

    def indy(self, cls, rest):
        mk = re.search(r"InvokeDynamic #(\d+):(\w+):(\S+)", comment(rest) or rest)
        if not mk: return "S.push(null);"
        bi, iname, desc = int(mk.group(1)), mk.group(2), mk.group(3)
        args, ret = arg_types(desc); n = len(args)
        pops = "".join(f"const v{i}=S.pop();" for i in reversed(range(n))); alist = ",".join(f"v{i}" for i in range(n))
        b = self.classes[cls].boot.get(bi, {})
        if b.get("kind") == "concat":
            recipe = b.get("recipe", "\u0001" * n)
            return f"{{{pops}S.push(R.concat({jsstr(recipe)},[{alist}]));}}"
        if b.get("kind") == "lambda" and b.get("target"):
            towner, tname, tdesc = b["target"]
            if self.is_mod(towner):
                f = self.fid(towner, tname, tdesc)
                return f"{{{pops}S.push(R.lambda({jsstr(iname)},(...xs)=>@@{f}@@({alist}{',' if n else ''}...xs)));}}"
            return f"{{{pops}S.push(R.lambdaApi({jsstr(towner)},{jsstr(self.api_name(towner, tname))},[{alist}]));}}"
        return "S.push(null);"

    def emit(self):
        parts = []
        for cn, info in self.classes.items():
            for key, m in info.methods.items():
                try: fn = self.method(cn, m)
                except Exception as ex: fn = None; self.unknown[f"err {cn}.{key}: {ex}"] = 1
                if fn: parts.append(f"M.def({jsstr(cn + '.' + key)},{fn});")
            parts.append(f"M.cls({jsstr(cn)},{jsstr(info.super)},{json.dumps([k for k in info.methods])});")
        return "\n".join(parts)
