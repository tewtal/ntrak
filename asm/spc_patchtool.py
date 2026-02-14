#!/usr/bin/env python3
"""
spc_patchtool.py (v4)

SPC700 disassembler/assembler for round-tripping tracker "extension" patches
between hex strings and readable assembly.

Highlights
- Parses Anomie's spc700.txt opcode table (auto-downloads if missing)
- Disassembles extension code bytes + hook patches into readable asm
- Assembles back into config JSON:
    - updates extension.code.address / extension.code.bytes
    - updates/creates hooks[].address / hooks[].bytes / hooks[].name
- Named patch blocks:
    .patch $1234, "Some Hook"
        call L5678
        nop
- Unknown opcodes decode as .byte $xx (preserves data bytes)
- db/dw and .byte/.word with label expressions (label +/- const)
- Multiple .org directives behave like a normal assembler:
    - first .org starts the main segment
    - later .org moves PC within the *current* segment (gap-filled with 00)

CLI:
  extract <config.json> <outdir> [--game ID] [--ext NAME]
  compile <config.json> <asmfile> [--game ID] [--ext NAME] [--inplace|--out PATH]
          [--replace-hooks] [--replace-extension] [--upsert]
  disasm-hex <org> <hexbytes>
  asm-hex <asmfile>    (prints main segment hex only)

Notes:
- Label semantics are standard: label binds to current PC. A label before the first .org binds to 0.
- SPC700 operand byte order: many multi-byte-operand encodings store operands "last-to-first".
  This tool uses spc700.txt templates and a reversal heuristic (with BBC/BBS/CBNE/DBNZ exceptions).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from urllib.request import urlopen

DEFAULT_OPDOC_URL = "https://raw.githubusercontent.com/gilligan/snesdev/master/docs/spc700.txt"
DEFAULT_OPDOC_LOCAL = "spc700.txt"

REGISTER_TOKENS = {"A", "X", "Y", "SP", "PSW", "YA", "C"}
IMPLIED_TOKENS = {"(X)", "(Y)", "(X)+"}

# Exceptions to operand-chunk reversal rule
NO_REVERSE_PREFIXES = ("BBC", "BBS", "CBNE", "DBNZ")


class SPC700Error(Exception):
    pass


def parse_int(expr: str) -> int:
    """Parse $hex, 0xhex, or decimal (optionally with leading -)."""
    s = expr.strip()
    if not s:
        raise SPC700Error("Empty number")
    neg = False
    if s.startswith("-"):
        neg = True
        s = s[1:].strip()
    if s.startswith("$"):
        v = int(s[1:], 16)
    else:
        v = int(s, 0)
    return -v if neg else v


def to_hex8(v: int) -> str:
    return f"${v & 0xFF:02X}"


def to_hex16(v: int) -> str:
    return f"${v & 0xFFFF:04X}"


def hexstr_to_bytes(s: str) -> bytes:
    s2 = re.sub(r"[^0-9A-Fa-f]", "", s)
    if len(s2) % 2 != 0:
        raise SPC700Error(f"Hex string has odd length: {len(s2)}")
    return bytes.fromhex(s2)


def bytes_to_hexstr(b: bytes, spaced: bool = False) -> str:
    if spaced:
        return " ".join(f"{x:02X}" for x in b)
    return "".join(f"{x:02X}" for x in b)


def sign8(v: int) -> int:
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def u8(v: int) -> int:
    return v & 0xFF


def u16(v: int) -> int:
    return v & 0xFFFF


def split_args_csv(s: str) -> List[str]:
    """
    Split comma-separated args while preserving quoted strings (INCLUDING quotes).
    Example:
      $1BBD, "VCMD Parameter Count"  ->  ["$1BBD", "\"VCMD Parameter Count\""]
    """
    out: List[str] = []
    cur: List[str] = []
    in_q = False
    qch = ""

    i = 0
    while i < len(s):
        ch = s[i]
        if not in_q:
            if ch in ('"', "'"):
                in_q = True
                qch = ch
                cur.append(ch)  # keep opening quote
            elif ch == ",":
                tok = "".join(cur).strip()
                if tok:
                    out.append(tok)
                cur = []
            else:
                cur.append(ch)
        else:
            cur.append(ch)
            if ch == qch:
                in_q = False
        i += 1

    tok = "".join(cur).strip()
    if tok:
        out.append(tok)
    return out


def parse_quoted_string(tok: str) -> Optional[str]:
    t = tok.strip()
    if len(t) >= 2 and ((t[0] == '"' and t[-1] == '"') or (t[0] == "'" and t[-1] == "'")):
        return t[1:-1]
    return None


def ensure_opdoc(path: Optional[Path]) -> Path:
    if path is not None:
        if not path.exists():
            raise SPC700Error(f"Opcode doc not found: {path}")
        return path

    local = Path(DEFAULT_OPDOC_LOCAL)
    if local.exists():
        return local

    try:
        data = urlopen(DEFAULT_OPDOC_URL, timeout=10).read()
    except Exception as e:
        raise SPC700Error(
            "Could not find spc700.txt locally and auto-download failed.\n"
            f"Download it once and place it next to this script: {DEFAULT_OPDOC_URL}\n"
            f"Error: {e}"
        )
    local.write_bytes(data)
    return local


@dataclass(frozen=True)
class OperandSpec:
    token: str
    size: int     # 0,1,2
    kind: str     # reg/imm/dp/abs/rel/dpbit/bitabs/ind_dp_y/ind_dp_x/ind_abs_x/const/implied

    def is_bytes(self) -> bool:
        return self.size > 0


@dataclass(frozen=True)
class OpcodeEntry:
    opcode: int
    template: str
    mnemonic: str
    operands: Tuple[OperandSpec, ...]
    length: int
    reverse_chunks: bool


def _normalize_template(template: str) -> str:
    t = re.sub(r"\s+", " ", template.strip())
    # dd/ds are synonyms of d in spc700.txt
    t = re.sub(r"\bdd\b", "d", t)
    t = re.sub(r"\bds\b", "d", t)
    return t


def _split_template(template: str) -> Tuple[str, List[str]]:
    t = _normalize_template(template)
    if " " not in t:
        return t, []
    mn, rest = t.split(" ", 1)
    rest = rest.strip()
    if not rest:
        return mn, []
    ops = [x.strip() for x in rest.split(",")] if "," in rest else [rest.strip()]
    ops = [o for o in ops if o]
    return mn, ops


def _operand_spec_from_token(tok: str) -> OperandSpec:
    t = tok.strip()

    if re.fullmatch(r"\d+", t):
        return OperandSpec(token=t, size=0, kind="const")

    if t in REGISTER_TOKENS:
        return OperandSpec(token=t, size=0, kind="reg")

    if t in IMPLIED_TOKENS:
        return OperandSpec(token=t, size=0, kind="implied")

    if t in ("#i", "up"):
        return OperandSpec(token=t, size=1, kind="imm")

    if t == "r":
        return OperandSpec(token=t, size=1, kind="rel")

    if t == "m.b":
        return OperandSpec(token=t, size=2, kind="bitabs")

    if re.fullmatch(r"d\.\d", t) or t == "d.#":
        return OperandSpec(token=t, size=1, kind="dpbit")

    if t == "d":
        return OperandSpec(token=t, size=1, kind="dp")

    if t in ("d+X", "d+Y"):
        return OperandSpec(token=t, size=1, kind="dp_index")

    if t.startswith("[d]+Y"):
        return OperandSpec(token=t, size=1, kind="ind_dp_y")
    if t.startswith("[d+X]"):
        return OperandSpec(token=t, size=1, kind="ind_dp_x")

    if t.startswith("!a"):
        return OperandSpec(token=t, size=2, kind="abs")

    if t.startswith("[!a+X]"):
        return OperandSpec(token=t, size=2, kind="ind_abs_x")

    return OperandSpec(token=t, size=0, kind="implied")


def _should_reverse(mnemonic: str, ops: Tuple[OperandSpec, ...]) -> bool:
    byte_ops = [o for o in ops if o.is_bytes()]
    if len(byte_ops) <= 1:
        return False
    for p in NO_REVERSE_PREFIXES:
        if mnemonic.startswith(p):
            return False
    return True


def parse_opcode_table(opdoc_text: str) -> Dict[int, OpcodeEntry]:
    """
    Parse the table from spc700.txt.

    Important:
    - cycle column can be '?' (SLEEP/STOP)
    - flags column can contain '0' or '1' (CLRP/SETP)
    """
    m = re.search(r"Mnemonic\s+Code\s+Bytes\s+Cyc\s+Operation\s+NVPBHIZC", opdoc_text)
    if not m:
        raise SPC700Error("Could not locate opcode table header in spc700.txt")

    table = opdoc_text[m.end():]

    entry_re = re.compile(
        r"(?P<mn>[A-Z][A-Z0-9]*.*?)\s+"
        r"(?P<code>[0-9A-F]{2})\s+"
        r"(?P<bytes>[1-3])\s+"
        r"(?P<cyc>\d+(?:/\d+)?|\?)\s+"
        r".*?\s+"
        r"(?P<flags>[NVPBHIZC\.01]{8})"
        r"(?:\s+|$)",
        re.DOTALL
    )

    opmap: Dict[int, OpcodeEntry] = {}
    for em in entry_re.finditer(table):
        tmpl_raw = em.group("mn").strip()
        code = int(em.group("code"), 16)
        length = int(em.group("bytes"))

        tmpl = _normalize_template(tmpl_raw)
        mnemonic, ops_tokens = _split_template(tmpl)
        ops = tuple(_operand_spec_from_token(o) for o in ops_tokens)
        reverse = _should_reverse(mnemonic, ops)
        opmap[code] = OpcodeEntry(
            opcode=code,
            template=tmpl,
            mnemonic=mnemonic,
            operands=ops,
            length=length,
            reverse_chunks=reverse,
        )

    if len(opmap) != 256:
        missing = [f"{i:02X}" for i in range(256) if i not in opmap]
        raise SPC700Error(f"Opcode table parse incomplete ({len(opmap)}/256). Missing: {missing}")

    return opmap


def load_isa(opdoc_path: Optional[Path]) -> Tuple[Dict[int, OpcodeEntry], Dict[str, List[OpcodeEntry]]]:
    p = ensure_opdoc(opdoc_path)
    text = p.read_text(errors="replace")
    opmap = parse_opcode_table(text)
    by_mn: Dict[str, List[OpcodeEntry]] = {}
    for e in opmap.values():
        by_mn.setdefault(e.mnemonic.upper(), []).append(e)
    return opmap, by_mn


def pack_bitabs(addr: int, bit: int) -> int:
    if bit < 0 or bit > 7:
        raise SPC700Error(f"Bit out of range: {bit}")
    return ((bit & 7) << 13) | (addr & 0x1FFF)


def unpack_bitabs(word: int) -> Tuple[int, int]:
    bit = (word >> 13) & 7
    addr = word & 0x1FFF
    return addr, bit


@dataclass
class ParsedOperand:
    kind: str  # reg/implied/imm/mem/bit/ind_dp_y/ind_x/const/ind
    text: str
    value: Optional[int] = None
    symbol: Optional[str] = None
    bit: Optional[int] = None
    index: Optional[str] = None
    force_abs: bool = False


def _is_ident(s: str) -> bool:
    return re.fullmatch(r"[A-Za-z_]\w*", s) is not None


def parse_operand(s: str) -> ParsedOperand:
    t = s.strip()
    up = t.upper()

    if up in REGISTER_TOKENS:
        return ParsedOperand(kind="reg", text=up)
    if up in IMPLIED_TOKENS:
        return ParsedOperand(kind="implied", text=up)
    if re.fullmatch(r"\d+", t):
        return ParsedOperand(kind="const", text=t, value=int(t, 10))

    if t.startswith("#"):
        expr = t[1:].strip()
        if _is_ident(expr):
            return ParsedOperand(kind="imm", text=t, symbol=expr)
        return ParsedOperand(kind="imm", text=t, value=parse_int(expr))

    m = re.match(r"^\[(.+)\]\+Y$", t, flags=re.IGNORECASE)
    if m:
        inner = m.group(1).strip()
        mem = parse_operand(inner)
        mem.kind = "ind_dp_y"
        mem.index = "Y"
        return mem

    m = re.match(r"^\[(.+)\]$", t)
    if m:
        inner = m.group(1).strip()
        mem = parse_operand(inner)
        if mem.index == "X":
            mem.kind = "ind_x"
        else:
            mem.kind = "ind"
        return mem

    if "." in t and re.search(r"\.\d+$", t):
        base, bit_s = t.rsplit(".", 1)
        bit = int(bit_s, 10)
        base = base.strip()
        force_abs = base.startswith("!")
        if force_abs:
            base = base[1:].strip()
        if _is_ident(base):
            return ParsedOperand(kind="bit", text=t, symbol=base, bit=bit, force_abs=force_abs)
        return ParsedOperand(kind="bit", text=t, value=parse_int(base), bit=bit, force_abs=force_abs)

    idx = None
    m = re.search(r"(\+X|\+Y)$", t, flags=re.IGNORECASE)
    base = t
    if m:
        idx = m.group(1).upper()[1:]
        base = t[:m.start()].strip()

    force_abs = False
    if base.startswith("!"):
        force_abs = True
        base = base[1:].strip()

    if _is_ident(base):
        return ParsedOperand(kind="mem", text=t, symbol=base, index=idx, force_abs=force_abs)

    return ParsedOperand(kind="mem", text=t, value=parse_int(base), index=idx, force_abs=force_abs)


@dataclass(frozen=True)
class Segment:
    kind: str  # "main" or "patch"
    origin: int
    data: bytes
    name: Optional[str] = None


class Disassembler:
    def __init__(self, opmap: Dict[int, OpcodeEntry]):
        self.opmap = opmap

    def scan_targets(self, data: bytes, origin: int) -> List[int]:
        targets: List[int] = []
        pc = origin
        i = 0
        while i < len(data):
            op = data[i]
            e = self.opmap.get(op)
            if e is None or i + e.length > len(data):
                i += 1
                pc += 1
                continue
            raw = data[i:i + e.length]
            _, t = self._format_entry(e, raw, pc, label_map=None)
            targets.extend(t)
            i += e.length
            pc += e.length
        return targets

    def disasm_segment(
        self,
        data: bytes,
        origin: int,
        label_map: Optional[Dict[int, str]] = None,
        emit_header: Optional[str] = None,  # ".org $xxxx" or ".patch $xxxx[, "Name"]"
    ) -> str:
        insns: List[Tuple[int, bytes, str]] = []
        pc = origin
        i = 0
        while i < len(data):
            op = data[i]
            e = self.opmap.get(op)
            if e is None:
                raw = bytes([op])
                insns.append((pc, raw, f".byte {to_hex8(op)}"))
                i += 1
                pc += 1
                continue
            if i + e.length > len(data):
                tail = data[i:]
                insns.append((pc, tail, ".byte " + ", ".join(to_hex8(x) for x in tail)))
                break
            raw = data[i:i + e.length]
            asm, _ = self._format_entry(e, raw, pc, label_map=label_map)
            insns.append((pc, raw, asm))
            i += e.length
            pc += e.length

        out: List[str] = []
        if emit_header:
            out.append(emit_header)

        for pc, raw, asm in insns:
            if label_map and pc in label_map:
                out.append(f"{label_map[pc]}:")
            out.append(f"    {asm:<28} ; {bytes_to_hexstr(raw, spaced=True)}")
        return "\n".join(out) + "\n"

    def _format_entry(
        self,
        e: OpcodeEntry,
        raw: bytes,
        pc: int,
        label_map: Optional[Dict[int, str]],
    ) -> Tuple[str, List[int]]:
        enc = raw[1:]

        byte_specs = [op for op in e.operands if op.is_bytes()]
        sizes = [op.size for op in byte_specs]

        sizes_for_read = list(reversed(sizes)) if e.reverse_chunks else sizes
        chunks_read: List[bytes] = []
        idx = 0
        for sz in sizes_for_read:
            chunks_read.append(enc[idx:idx + sz])
            idx += sz

        chunks = list(reversed(chunks_read)) if e.reverse_chunks else chunks_read

        def fmt_addr(addr: int) -> str:
            if label_map and addr in label_map:
                return label_map[addr]
            return to_hex16(addr)

        final_ops: List[str] = []
        targets: List[int] = []
        ci = 0

        for spec in e.operands:
            if not spec.is_bytes():
                final_ops.append(spec.token)
                continue

            b = chunks[ci]
            ci += 1

            if spec.kind == "imm":
                v = f"#{to_hex8(b[0])}"
            elif spec.kind in ("dp", "dp_index", "ind_dp_y", "ind_dp_x"):
                v = to_hex8(b[0])
            elif spec.kind in ("abs", "ind_abs_x"):
                w = b[0] | (b[1] << 8)
                v = fmt_addr(w)
                targets.append(w)
            elif spec.kind == "rel":
                off = sign8(b[0])
                tgt = u16(pc + e.length + off)
                v = fmt_addr(tgt)
                targets.append(tgt)
            elif spec.kind == "dpbit":
                bit_m = re.search(r"\.(\d)", spec.token)
                bit = int(bit_m.group(1)) if bit_m else 0
                v = f"{to_hex8(b[0])}.{bit}"
            elif spec.kind == "bitabs":
                w = b[0] | (b[1] << 8)
                addr, bit = unpack_bitabs(w)
                v = f"{fmt_addr(addr)}.{bit}"
                targets.append(addr)
            else:
                v = bytes_to_hexstr(b, spaced=True)

            tok = spec.token
            if tok == "[d]+Y":
                v = f"[{v}]+Y"
            elif tok == "[d+X]":
                v = f"[{v}+X]"
            elif tok == "[!a+X]":
                v = f"[{v}+X]"
            elif tok.endswith("+X"):
                v = f"{v}+X"
            elif tok.endswith("+Y"):
                v = f"{v}+Y"

            final_ops.append(v)

        asm = e.mnemonic
        if final_ops:
            asm += " " + ", ".join(final_ops)
        return asm, targets


# ---------- Assembler (multi-segment: main + patch blocks) ----------

@dataclass(frozen=True)
class ValueExpr:
    """Integer or label [+/- const]."""
    label: Optional[str] = None
    addend: int = 0
    value: Optional[int] = None  # if purely numeric

    def eval(self, labels: Dict[str, int]) -> int:
        if self.value is not None:
            return self.value + self.addend
        if self.label is None:
            raise SPC700Error("Bad ValueExpr")
        if self.label not in labels:
            raise SPC700Error(f"Unresolved symbol: {self.label}")
        return labels[self.label] + self.addend


def parse_value_expr(s: str) -> ValueExpr:
    t = s.strip()
    if t.startswith("$") or t.startswith("0x") or re.fullmatch(r"-?\d+", t):
        return ValueExpr(value=parse_int(t))
    m = re.fullmatch(r"([A-Za-z_]\w*)(\s*([+\-])\s*(\$[0-9A-Fa-f]+|0x[0-9A-Fa-f]+|\d+))?", t)
    if not m:
        raise SPC700Error(f"Bad expression: {s}")
    lab = m.group(1)
    add = 0
    if m.group(2):
        sign = m.group(3)
        num = parse_int(m.group(4))
        add = num if sign == "+" else -num
    return ValueExpr(label=lab, addend=add)


@dataclass
class PatchResult:
    main_org: int
    main_code: bytes
    patch_segments: List[Tuple[int, Optional[str], bytes]]  # (addr, name, bytes)


class Assembler:
    def __init__(self, by_mnemonic: Dict[str, List[OpcodeEntry]]):
        self.by_mn = by_mnemonic

    def assemble_text(self, text: str) -> PatchResult:
        lines = self._preprocess_lines(text)

        main_org: Optional[int] = None
        mode: Optional[str] = None  # "main" or "patch"
        pc = 0

        labels: Dict[str, int] = {}
        items: List[Tuple[int, Tuple]] = []  # (line, item)

        def start_segment(kind: str, origin: int, seg_name: Optional[str] = None):
            nonlocal mode, pc
            mode = kind
            pc = origin
            items.append((ln, ("seg", kind, origin, seg_name)))

        # Pass 1: labels + lengths
        for ln, line in lines:
            if line.endswith(":"):
                name = line[:-1].strip()
                if not _is_ident(name):
                    raise SPC700Error(f"Invalid label '{name}' on line {ln}")
                labels[name] = pc
                continue

            directive = self._parse_directive(line)
            if directive:
                kind, args = directive

                if kind == "org":
                    origin = parse_int(args[0])
                    if main_org is None:
                        main_org = origin
                        start_segment("main", origin, None)
                    else:
                        if mode is None:
                            raise SPC700Error(f"Line {ln}: .org before any segment")
                        items.append((ln, ("setpc", origin)))
                        pc = origin
                    continue

                if kind == "patch":
                    addr = parse_int(args[0])

                    patch_name: Optional[str] = None
                    rest = args[1:]
                    if rest:
                        maybe_name = parse_quoted_string(rest[0])
                        if maybe_name is not None:
                            patch_name = maybe_name
                            rest = rest[1:]

                    if not rest:
                        start_segment("patch", addr, patch_name)
                    else:
                        bs = [u8(parse_int(a)) for a in rest]
                        items.append((ln, ("patch_inline", addr, patch_name, bs)))
                    continue

                if mode is None:
                    raise SPC700Error(f"Line {ln}: directive '{kind}' before any .org/.patch")

                if kind in ("byte", "db"):
                    vs = [parse_value_expr(a) for a in args]
                    pc += len(vs)
                    items.append((ln, ("byte", mode, vs)))
                    continue

                if kind in ("word", "dw"):
                    vs = [parse_value_expr(a) for a in args]
                    pc += 2 * len(vs)
                    items.append((ln, ("word", mode, vs)))
                    continue

                raise SPC700Error(f"Unknown directive .{kind} on line {ln}")

            if mode is None:
                raise SPC700Error(f"Line {ln}: instruction before any .org/.patch")

            mnem, ops_s = self._parse_instruction(line)
            ops = [parse_operand(o) for o in ops_s]
            entry = self._select_entry(mnem, ops, pc, labels=None)
            pc += entry.length
            items.append((ln, ("insn", mode, entry, mnem, ops)))

        if main_org is None:
            raise SPC700Error("Missing .org directive (main code segment)")

        # Pass 2: encode into buffers
        main_buf = bytearray()
        patches: List[Tuple[int, Optional[str], bytearray]] = []
        current_buf: Optional[bytearray] = None
        current_kind: Optional[str] = None
        current_origin: Optional[int] = None
        pc = 0

        def set_active(kind: str, origin: int, name: Optional[str]):
            nonlocal current_buf, current_kind, current_origin, pc
            current_kind = kind
            current_origin = origin
            pc = origin
            if kind == "main":
                current_buf = main_buf
            else:
                pb = bytearray()
                patches.append((origin, name, pb))
                current_buf = pb

        def write_bytes2(b: bytes):
            nonlocal pc
            assert current_buf is not None
            current_buf.extend(b)
            pc += len(b)

        for ln, it in items:
            tag = it[0]

            if tag == "seg":
                _, kind, origin, name = it
                set_active(kind, origin, name)
                continue

            if tag == "setpc":
                _, new_pc = it
                if current_buf is None or current_origin is None:
                    raise SPC700Error("Internal: .org with no active segment")
                if new_pc < pc:
                    raise SPC700Error(
                        f"Line {ln}: .org moves backwards ({to_hex16(pc)} -> {to_hex16(new_pc)})"
                    )
                if new_pc > pc:
                    # Fill gaps with 00 by default
                    write_bytes2(bytes([0x00]) * (new_pc - pc))
                pc = new_pc
                continue

            if tag == "patch_inline":
                _, addr, name, bs = it
                patches.append((addr, name, bytearray(bs)))
                continue

            if current_buf is None or current_kind is None or current_origin is None:
                raise SPC700Error("Internal: no active segment during encoding")

            if tag == "byte":
                _, _kind, vs = it
                out = bytearray()
                for ve in vs:
                    out.append(u8(ve.eval(labels)))
                write_bytes2(bytes(out))
                continue

            if tag == "word":
                _, _kind, vs = it
                out = bytearray()
                for ve in vs:
                    w = u16(ve.eval(labels))
                    out.append(w & 0xFF)
                    out.append((w >> 8) & 0xFF)
                write_bytes2(bytes(out))
                continue

            if tag == "insn":
                _, _kind, entry, mnem, ops = it
                entry2 = self._select_entry(mnem, ops, pc, labels=labels, prefer_exact=True)
                if entry2.length != entry.length or entry2.opcode != entry.opcode:
                    raise SPC700Error(
                        f"Line {ln}: instruction form changed after label resolution "
                        f"({entry.template} -> {entry2.template}). Hint: use ! to force absolute addressing."
                    )
                encoded = self._encode(entry, ops, pc, labels)
                write_bytes2(encoded)
                continue

            raise SPC700Error("Internal: unknown item type")

        patch_segments = [(addr, name, bytes(buf)) for addr, name, buf in patches]
        return PatchResult(main_org=main_org, main_code=bytes(main_buf), patch_segments=patch_segments)

    def _preprocess_lines(self, text: str) -> List[Tuple[int, str]]:
        out: List[Tuple[int, str]] = []
        for i, line in enumerate(text.splitlines(), start=1):
            line = line.split(";", 1)[0].strip()
            if not line:
                continue
            out.append((i, line))
        return out

    def _parse_directive(self, line: str) -> Optional[Tuple[str, List[str]]]:
        m = re.match(r"^\.(\w+)\s*(.*)$", line)
        if m:
            kind = m.group(1).lower()
            rest = m.group(2).strip()
            args = split_args_csv(rest) if rest else []
            if kind == "org" and len(args) != 1:
                raise SPC700Error(".org takes 1 argument")
            if kind == "patch" and len(args) < 1:
                raise SPC700Error(".patch needs at least an address")
            return kind, args

        m2 = re.match(r"^(db|dw)\s+(.*)$", line, flags=re.IGNORECASE)
        if m2:
            kind = m2.group(1).lower()
            args = split_args_csv(m2.group(2))
            return kind, args

        return None

    def _parse_instruction(self, line: str) -> Tuple[str, List[str]]:
        parts = line.split(None, 1)
        mnem = parts[0].upper()
        ops: List[str] = []
        if len(parts) == 2:
            rest = parts[1].strip()
            ops = [o.strip() for o in rest.split(",") if o.strip()] if "," in rest else ([rest] if rest else [])
        return mnem, ops

    def _select_entry(
        self,
        mnem: str,
        ops: List[ParsedOperand],
        pc: int,
        labels: Optional[Dict[str, int]],
        prefer_exact: bool = False,
    ) -> OpcodeEntry:
        cands = self.by_mn.get(mnem.upper(), [])
        if not cands:
            raise SPC700Error(f"Unknown mnemonic: {mnem}")

        best: Optional[Tuple[int, OpcodeEntry]] = None
        for e in cands:
            ok, score = self._match_entry(e, ops, labels, pc, prefer_exact=prefer_exact)
            if not ok:
                continue
            if best is None or score > best[0]:
                best = (score, e)

        if best is None:
            sigs = sorted(set(e.template for e in cands))
            raise SPC700Error(
                f"No matching form for '{mnem} {', '.join(o.text for o in ops)}'. "
                f"Known forms include: {sigs[:10]} ..."
            )
        return best[1]

    def _match_entry(
        self,
        e: OpcodeEntry,
        ops: List[ParsedOperand],
        labels: Optional[Dict[str, int]],
        pc: int,
        prefer_exact: bool,
    ) -> Tuple[bool, int]:
        if len(ops) != len(e.operands):
            return False, 0

        score = 0
        for spec, op in zip(e.operands, ops):
            if spec.kind == "reg":
                if op.kind != "reg" or op.text.upper() != spec.token:
                    return False, 0
                score += 3
                continue

            if spec.kind == "implied":
                if op.kind != "implied" or op.text.upper() != spec.token:
                    return False, 0
                score += 3
                continue

            if spec.kind == "const":
                if op.kind != "const" or op.value != int(spec.token):
                    return False, 0
                score += 3
                continue

            if spec.kind == "imm":
                if op.kind != "imm":
                    return False, 0
                score += 2
                continue

            if spec.kind == "rel":
                score += 2
                continue

            if spec.kind == "dpbit":
                if op.kind != "bit":
                    return False, 0
                bit_m = re.search(r"\.(\d)", spec.token)
                bit = int(bit_m.group(1)) if bit_m else None
                if bit is not None and op.bit != bit:
                    return False, 0
                if op.force_abs:
                    return False, 0
                addr = self._resolve_address(op, labels)
                if addr is None:
                    score += 1
                else:
                    if addr > 0xFF:
                        return False, 0
                    score += 2
                continue

            if spec.kind == "bitabs":
                if op.kind != "bit":
                    return False, 0
                score += 2
                continue

            if spec.kind in ("dp", "dp_index", "ind_dp_y", "ind_dp_x"):
                if spec.kind == "ind_dp_y":
                    if op.kind != "ind_dp_y":
                        return False, 0
                elif spec.kind == "ind_dp_x":
                    if op.kind != "ind_x":
                        return False, 0
                else:
                    if op.kind != "mem":
                        return False, 0

                if spec.kind == "dp_index":
                    need = spec.token[-1]
                    if op.index != need:
                        return False, 0
                elif spec.kind == "dp":
                    if op.index is not None:
                        return False, 0

                if op.force_abs:
                    return False, 0

                addr = self._resolve_address(op, labels)
                if addr is None:
                    score += 1
                else:
                    if addr > 0xFF:
                        return False, 0
                    score += 2
                continue

            if spec.kind in ("abs", "ind_abs_x"):
                if spec.kind == "ind_abs_x":
                    if op.kind != "ind_x":
                        return False, 0
                else:
                    if op.kind != "mem":
                        return False, 0

                if spec.token.endswith("+X") and op.index != "X":
                    return False, 0
                if spec.token.endswith("+Y") and op.index != "Y":
                    return False, 0
                if spec.token == "!a" and op.index is not None:
                    return False, 0

                addr = self._resolve_address(op, labels)
                if addr is None:
                    score += 2
                else:
                    if addr > 0xFFFF:
                        return False, 0
                    score += 2
                continue

            return False, 0

        return True, score

    def _resolve_address(self, op: ParsedOperand, labels: Optional[Dict[str, int]]) -> Optional[int]:
        if op.value is not None:
            return op.value
        if labels is not None and op.symbol is not None:
            return labels.get(op.symbol)
        return None

    def _encode(self, e: OpcodeEntry, ops: List[ParsedOperand], pc: int, labels: Dict[str, int]) -> bytes:
        chunks: List[bytes] = []

        for spec, op in zip(e.operands, ops):
            if not spec.is_bytes():
                continue

            if spec.kind == "imm":
                v = op.value if op.value is not None else labels[op.symbol]  # type: ignore
                chunks.append(bytes([u8(v)]))

            elif spec.kind == "rel":
                tgt = self._resolve_address(op, labels)
                if tgt is None:
                    raise SPC700Error(f"Unresolved branch target: {op.text}")
                off = tgt - (pc + e.length)
                if off < -128 or off > 127:
                    raise SPC700Error(f"Branch out of range from {to_hex16(pc)} to {to_hex16(tgt)} (off={off})")
                chunks.append(bytes([u8(off)]))

            elif spec.kind == "dpbit":
                addr = self._resolve_address(op, labels)
                if addr is None:
                    raise SPC700Error(f"Unresolved dp.bit address: {op.text}")
                chunks.append(bytes([u8(addr)]))

            elif spec.kind == "bitabs":
                addr = self._resolve_address(op, labels)
                if addr is None:
                    raise SPC700Error(f"Unresolved abs.bit address: {op.text}")
                w = pack_bitabs(addr, op.bit or 0)
                chunks.append(bytes([w & 0xFF, (w >> 8) & 0xFF]))

            elif spec.kind in ("dp", "dp_index", "ind_dp_y", "ind_dp_x"):
                addr = self._resolve_address(op, labels)
                if addr is None:
                    raise SPC700Error(f"Unresolved dp address: {op.text}")
                chunks.append(bytes([u8(addr)]))

            elif spec.kind in ("abs", "ind_abs_x"):
                addr = self._resolve_address(op, labels)
                if addr is None:
                    raise SPC700Error(f"Unresolved abs address: {op.text}")
                chunks.append(bytes([addr & 0xFF, (addr >> 8) & 0xFF]))

            else:
                raise SPC700Error(f"Unsupported operand kind in encoding: {spec.kind}")

        if e.reverse_chunks:
            chunks = list(reversed(chunks))

        out = bytearray([e.opcode])
        for ch in chunks:
            out.extend(ch)

        if len(out) != e.length:
            raise SPC700Error(f"Internal length mismatch encoding {e.template}: got {len(out)}, expected {e.length}")

        return bytes(out)


# ---------- Config integration ----------

def _segment_ranges(segments: List[Segment]) -> List[Tuple[int, int]]:
    return [(s.origin, s.origin + len(s.data)) for s in segments]


def _in_any_range(addr: int, ranges: List[Tuple[int, int]]) -> bool:
    for a, b in ranges:
        if a <= addr < b:
            return True
    return False


def extract_from_config(
    config_path: Path,
    outdir: Path,
    game_id: Optional[str],
    ext_name: Optional[str],
    opdoc: Optional[Path],
) -> None:
    opmap, _ = load_isa(opdoc)
    dis = Disassembler(opmap)
    cfg = json.loads(config_path.read_text())
    outdir.mkdir(parents=True, exist_ok=True)

    def write_ext(game: dict, ext: dict) -> None:
        segs: List[Segment] = []
        main_addr = parse_int(ext["code"]["address"])
        main_bytes = hexstr_to_bytes(ext["code"]["bytes"])
        segs.append(Segment(kind="main", origin=main_addr, data=main_bytes, name=None))

        hooks = ext.get("hooks", [])
        for h in hooks:
            ha = parse_int(h["address"])
            hb = hexstr_to_bytes(h["bytes"])
            segs.append(Segment(kind="patch", origin=ha, data=hb, name=h.get("name")))

        ranges = _segment_ranges(segs)

        targets: List[int] = []
        for s in segs:
            targets.extend(dis.scan_targets(s.data, s.origin))

        label_map: Dict[int, str] = {}
        for t in sorted(set(targets)):
            if _in_any_range(t, ranges):
                label_map.setdefault(t, f"L{t:04X}")

        out_lines: List[str] = [
            f"; @game {game['id']}",
            f"; @extension {ext['name']}",
            "",
        ]

        out_lines.append(
            dis.disasm_segment(
                main_bytes,
                main_addr,
                label_map=label_map,
                emit_header=f".org {to_hex16(main_addr)}",
            ).rstrip()
        )

        for hook_idx, h in enumerate(hooks):
            ha = parse_int(h["address"])
            hb = hexstr_to_bytes(h["bytes"])
            hname = h.get("name")

            if hname:
                header = f'.patch {to_hex16(ha)}, "{hname}" ; hook_idx: {hook_idx} hook_key: "{hname}"'
            else:
                header = f".patch {to_hex16(ha)} ; hook_idx: {hook_idx} hook_key: <none>"

            out_lines.append("")
            out_lines.append(
                dis.disasm_segment(
                    hb,
                    ha,
                    label_map=label_map,
                    emit_header=header,
                ).rstrip()
            )

        out_text = "\n".join(out_lines).rstrip() + "\n"
        out_name = f"{game['id']}__{re.sub(r'[^A-Za-z0-9_]+','_', ext['name']).strip('_')}.asm"
        out_path = outdir / out_name
        out_path.write_text(out_text)
        print(f"Wrote {out_path}")

    for game in cfg:
        if game_id and game["id"] != game_id:
            continue
        for ext in game.get("extensions", []):
            if ext_name and ext["name"] != ext_name:
                continue
            write_ext(game, ext)


def compile_into_config(
    config_path: Path,
    asm_path: Path,
    game_id: Optional[str],
    ext_name: Optional[str],
    out_path: Optional[Path],
    inplace: bool,
    opdoc: Optional[Path],
    replace_hooks: bool,
    replace_extension: bool,
    upsert: bool,
) -> None:
    _, by_mn = load_isa(opdoc)
    asm_text = asm_path.read_text()
    asm = Assembler(by_mn)
    res = asm.assemble_text(asm_text)

    cfg = json.loads(config_path.read_text())

    meta_game = None
    meta_ext = None
    for line in asm_text.splitlines():
        line = line.strip()
        if line.startswith(";"):
            if line.lower().startswith("; @game"):
                meta_game = line.split(None, 2)[2].strip()
            if line.lower().startswith("; @extension"):
                meta_ext = line.split(None, 2)[2].strip()
        else:
            break

    gid = game_id or meta_game
    ename = ext_name or meta_ext
    if not gid or not ename:
        raise SPC700Error("Need --game and --ext, or include '; @game ...' and '; @extension ...' in the asm file.")

    # Find game
    game_obj = None
    for game in cfg:
        if game.get("id") == gid:
            game_obj = game
            break
    if game_obj is None:
        raise SPC700Error(f"Game '{gid}' not found in config")

    exts = game_obj.setdefault("extensions", [])

    # Find extension
    ext_obj = None
    for ext in exts:
        if ext.get("name") == ename:
            ext_obj = ext
            break

    def build_hooks_from_segments(patch_segments: List[Tuple[int, Optional[str], bytes]]) -> List[dict]:
        hooks_new: List[dict] = []
        for addr, pname, b in patch_segments:
            hooks_new.append({
                "name": pname or f"patch_{addr:04X}",
                "address": f"0x{addr:04X}",
                "bytes": bytes_to_hexstr(b, spaced=False),
            })
        return hooks_new

    code_new = {
        "address": f"0x{res.main_org:04X}",
        "bytes": bytes_to_hexstr(res.main_code, spaced=False),
    }
    hooks_new = build_hooks_from_segments(res.patch_segments)

    if ext_obj is None:
        if not (upsert or replace_extension):
            raise SPC700Error(f"Extension '{ename}' not found in game '{gid}' (use --upsert or --replace-extension)")
        ext_obj = {
            "name": ename,
            "description": "",
            "code": code_new,
            "hooks": hooks_new,
            "vcmds": [],
        }
        exts.append(ext_obj)
    else:
        if replace_extension:
            # Replace whole block (minimal fields). Keeps only name + refreshed fields.
            ext_obj.clear()
            ext_obj.update({
                "name": ename,
                "description": "",
                "code": code_new,
                "hooks": hooks_new,
                "vcmds": [],
            })
        else:
            # Always update code
            ext_obj["code"] = code_new

            if replace_hooks:
                ext_obj["hooks"] = hooks_new
            else:
                # Merge into existing hooks by address (legacy behavior)
                hooks = ext_obj.get("hooks", [])
                hook_by_addr = {parse_int(h["address"]): h for h in hooks if "address" in h}
                for addr, pname, b in res.patch_segments:
                    if addr in hook_by_addr:
                        hook_by_addr[addr]["bytes"] = bytes_to_hexstr(b, spaced=False)
                        if pname:
                            hook_by_addr[addr]["name"] = pname
                    else:
                        hooks.append({
                            "name": pname or f"patch_{addr:04X}",
                            "address": f"0x{addr:04X}",
                            "bytes": bytes_to_hexstr(b, spaced=False),
                        })
                ext_obj["hooks"] = hooks

    out = out_path
    if inplace:
        out = config_path
    if out is None:
        out = config_path.with_suffix(".out.json")

    Path(out).write_text(json.dumps(cfg, indent=4))
    print(f"Wrote {out}")


def disasm_hex(addr: int, hexstr: str, opdoc: Optional[Path]) -> None:
    opmap, _ = load_isa(opdoc)
    dis = Disassembler(opmap)
    b = hexstr_to_bytes(hexstr)
    sys.stdout.write(dis.disasm_segment(b, origin=addr, label_map=None, emit_header=f".org {to_hex16(addr)}"))


def asm_hex(asm_path: Path, opdoc: Optional[Path]) -> None:
    _, by_mn = load_isa(opdoc)
    asm = Assembler(by_mn)
    res = asm.assemble_text(asm_path.read_text())
    # Prints ONLY the main segment hex (hooks/patch segments are not included in this output)
    print(bytes_to_hexstr(res.main_code, spaced=False))


def main(argv: Optional[List[str]] = None) -> None:
    ap = argparse.ArgumentParser(
        description="SPC700 patch disasm/asm helper for tracker extension bytes (named .patch blocks; normal .org behavior)."
    )
    ap.add_argument(
        "--opdoc",
        type=str,
        default=None,
        help="Path to spc700.txt. If omitted, uses ./spc700.txt or auto-downloads.",
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_ex = sub.add_parser("extract", help="Extract extensions from config.json into .asm files")
    p_ex.add_argument("config", type=str)
    p_ex.add_argument("outdir", type=str)
    p_ex.add_argument("--game", type=str, default=None)
    p_ex.add_argument("--ext", type=str, default=None)

    p_co = sub.add_parser("compile", help="Assemble an .asm file and write back into config.json")
    p_co.add_argument("config", type=str)
    p_co.add_argument("asmfile", type=str)
    p_co.add_argument("--game", type=str, default=None)
    p_co.add_argument("--ext", type=str, default=None)
    p_co.add_argument("--inplace", action="store_true", help="Overwrite input config.json")
    p_co.add_argument("--out", type=str, default=None)
    p_co.add_argument("--replace-hooks", action="store_true", help="Replace hooks[] exactly with what the ASM produced")
    p_co.add_argument("--replace-extension", action="store_true", help="Replace the entire extension object (minimal) and add if missing")
    p_co.add_argument("--upsert", action="store_true", help="If extension doesn't exist, create it (minimal)")

    p_dx = sub.add_parser("disasm-hex", help="Disassemble a raw hex string (as a .org segment)")
    p_dx.add_argument("org", type=str, help="Origin address (e.g., 0x56E2)")
    p_dx.add_argument("hex", type=str, help="Hex bytes (spaces ok)")

    p_ax = sub.add_parser("asm-hex", help="Assemble an .asm file and print main segment hex")
    p_ax.add_argument("asmfile", type=str)

    args = ap.parse_args(argv)
    opdoc = Path(args.opdoc) if args.opdoc else None

    if args.cmd == "extract":
        extract_from_config(Path(args.config), Path(args.outdir), args.game, args.ext, opdoc)
    elif args.cmd == "compile":
        compile_into_config(
            Path(args.config),
            Path(args.asmfile),
            args.game,
            args.ext,
            Path(args.out) if args.out else None,
            args.inplace,
            opdoc,
            replace_hooks=args.replace_hooks,
            replace_extension=args.replace_extension,
            upsert=args.upsert,
        )
    elif args.cmd == "disasm-hex":
        disasm_hex(parse_int(args.org), args.hex, opdoc)
    elif args.cmd == "asm-hex":
        asm_hex(Path(args.asmfile), opdoc)
    else:
        raise SPC700Error(f"Unknown command: {args.cmd}")


if __name__ == "__main__":
    try:
        main()
    except SPC700Error as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(2)
