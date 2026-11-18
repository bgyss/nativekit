#!/usr/bin/env python3
"""Static discovery of a computed transfer's targets (`jumptable.py`).

An authored cartridge holds each form the code map recognises (a table of
long pointers, a table of word offsets, a table of branches reached through
the program counter, an address loaded on two paths) and the cases it must
leave alone (a register a call may have changed, an address taken from the
stack, a row that is not code, a neighbouring table, a row past the first
handler). No trace is needed: the reset vector reaches every site. A trace
that saw a target outside a table refuses that table. Nothing here comes
from a game.
"""
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "consoles/md/tools/decomp"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import census_fixture  # noqa: E402
import codemap  # noqa: E402
import intake  # noqa: E402

M68K = """\
        org     $200
start:  bsr.w   fa
        bsr.w   fb
        bsr.w   fc
        bsr.w   fd
        bsr.w   fe
        bsr.w   ff
        bsr.w   fg
stop:   bra.s   stop

        org     $220
fa:     lea     (ta,pc),a0
        add.w   d0,d0
        add.w   d0,d0
        movea.l (0,a0,d0.w),a0
        jsr     (a0)
        rts
        org     $240
ta:     dc.l    a0h,0,a1h,a2h
        dc.l    notcode
        dc.l    a3h
        org     $260
a0h:    moveq   #10,d7
        rts
a1h:    moveq   #11,d7
        rts
a2h:    moveq   #12,d7
        rts
a3h:    moveq   #13,d7
        rts
notcode:
        dc.w    $A000,$A000

        org     $280
fb:     add.w   d0,d0
        add.w   d0,d0
        jmp     (tb,pc,d0.w)
tb:     bra.w   b0h
        bra.w   b1h
b0h:    moveq   #20,d7
        rts
b1h:    moveq   #21,d7
        rts

        org     $2A0
fc:     add.w   d0,d0
        lea     (tc,pc),a2
        move.w  (0,a2,d0.w),d0
        jsr     (0,a2,d0.w)
        rts
tc:     dc.w    c0h-tc,c1h-tc
c0h:    ori.b   #0,d0           ; its first word, read as a row, would name the table
        moveq   #30,d7
        rts
c1h:    moveq   #31,d7
        rts

        org     $2C0
fd:     lea     (d0h,pc),a0
        tst.w   d1
        beq.s   fd1
        lea     (d1h,pc),a0
fd1:    jsr     (a0)
        rts
        org     $2E0
d0h:    moveq   #40,d7
        rts
d1h:    moveq   #41,d7
        rts

        org     $300
fe:     lea     (te1,pc),a0
        adda.w  ($FF0000).l,a0
        movea.l (a0),a0
        jsr     (a0)
        lea     (te2,pc),a0
        adda.w  ($FF0000).l,a0
        movea.l (a0),a0
        jmp     (a0)
        org     $330
te1:    dc.l    e0h,e1h
te2:    dc.l    e2h,e3h
e0h:    moveq   #50,d7
        rts
e1h:    moveq   #51,d7
        rts
e2h:    moveq   #52,d7
        rts
e3h:    moveq   #53,d7
        rts

        org     $360
ff:     movea.l (a7)+,a0
        jmp     (a0)

        org     $380
fg:     lea     (g0h,pc),a0
        bsr.w   fgsub
        jsr     (a0)
        bra.w   fi
fgsub:  rts
        org     $3A0
g0h:    moveq   #60,d7
        rts

        org     $3B0
fh:     add.w   d0,d0
        jmp     (th,pc,d0.w)
th:     move.l  (a0)+,(a1)
        move.l  (a0)+,(a1)
        rts

        org     $3F0
stray:  rte
        org     $3FE
        dc.w    0

        org     $400
fi:     bsr.w   fi2
        bsr.w   fj2
        bra.w   fh
fi2:    add.w   d0,d0           ; a table of word offsets read from the PC's table
        move.w  (ti,pc,d0.w),d0
        jmp     (ti,pc,d0.w)
ti:     dc.w    i0h-ti,i1h-ti
i0h:    moveq   #70,d7
        rts
i1h:    moveq   #71,d7
        rts
fj2:    lea     (tj,pc),a3      ; word offsets read through a register, added to the PC's base
        add.w   d0,d0
        move.w  (0,a3,d0.w),d0
        jmp     (jb,pc,d0.w)
tj:     dc.w    j0h-jb,j1h-jb
jb:
j0h:    moveq   #80,d7
        rts
j1h:    moveq   #81,d7
        rts
        org     $5FE
        dc.w    0
"""
SITE_A, SITE_B, SITE_C, SITE_D, SITE_E1, SITE_E2, SITE_F, SITE_G = (
    0x22C, 0x284, 0x2AA, 0x2CC, 0x30C, 0x31A, 0x362, 0x388)
SITE_H, INLINE = 0x3B2, 0x3B6
SITE_I, SITE_J = 0x412, 0x42C
HANDLERS = {
    "a0h": 0x260, "a1h": 0x264, "a2h": 0x268, "a3h": 0x26C, "notcode": 0x270,
    "b0h": 0x290, "b1h": 0x294, "c0h": 0x2B4, "c1h": 0x2BC, "d0h": 0x2E0, "d1h": 0x2E4,
    "e0h": 0x340, "e1h": 0x344, "e2h": 0x348, "e3h": 0x34C, "g0h": 0x3A0,
    "i0h": 0x41A, "i1h": 0x41E, "j0h": 0x434, "j1h": 0x438,
}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def build_rom(directory):
    directory.mkdir(parents=True, exist_ok=True)
    code = census_fixture._assemble("tables", "vasmm68k_mot", M68K, directory,
                                    ("-m68000", "-no-opt"))
    require(len(code) == 0x400, "table fixture: 68000 image is %d bytes, not 1,024" % len(code))
    rom = bytearray(0x8000)
    rom[0:4] = (0x00FFFF00).to_bytes(4, "big")
    rom[4:8] = (0x200).to_bytes(4, "big")
    for offset in range(8, 0x100, 4):
        rom[offset:offset + 4] = (0x3F0).to_bytes(4, "big")
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x120:0x150] = b"NATIVEKIT TABLE CONTROL".ljust(48, b" ")
    rom[0x150:0x180] = b"NATIVEKIT TABLE CONTROL".ljust(48, b" ")
    rom[0x1F0:0x200] = b"JUE             "
    rom[0x200:0x600] = code
    path = directory / "table-control.bin"
    path.write_bytes(rom)
    return path


class Saw:
    """Evidence that a trace ran `site` and saw it transfer to `target`."""

    def __init__(self, site, target):
        self.site, self.target = site, target

    def replay(self, ingest):
        ingest.begin()
        ingest.merge({self.site, self.target}, {(self.site, self.target)}, set())
        ingest.end()


def rom_document(rom, evidence=(), **options):
    spaces = codemap.build(rom, evidence, **options)
    return codemap.document(rom, spaces)["spaces"][0]


def evidence_at(space, address):
    return next((i["evidence"] for i in space["instructions"] if i["at"] == address), None)


def tables_at(space, site, status="accepted"):
    return [table for table in space["tables"]
            if table["site"] == site and table["status"] == status]


def forms(rom):
    space = rom_document(rom)
    table = {name: evidence_at(space, address) for name, address in HANDLERS.items()}
    for name in ("a0h", "a1h", "a2h", "b0h", "b1h", "c0h", "c1h", "d0h", "d1h",
                 "e0h", "e1h", "e2h", "e3h", "i0h", "i1h", "j0h", "j1h"):
        require(table[name] == "table", "%s is %r, not code by table evidence" % (name, table[name]))
    for name in ("a3h", "notcode", "g0h"):
        require(table[name] is None, "%s was decoded as code without evidence" % name)

    def one(site, kind, at, rows, targets):
        found = tables_at(space, site)
        require(len(found) == 1 and found[0]["kind"] == kind and found[0]["at"] == at and
                found[0]["rows"] == rows and found[0]["targets"] == targets,
                "site %04X: expected one %s table at %04X of %d rows, got %r"
                % (site, kind, at, rows, found))

    # a null row is passed over; the row that is not code ends the table before a3h's row
    one(SITE_A, "pointer", 0x240, 4, [0x260, 0x264, 0x268])
    # rows are the branches themselves; the table ends where its first target begins
    one(SITE_B, "branch", 0x288, 2, [0x288, 0x28C])
    one(SITE_C, "offset", 0x2B0, 2, [0x2B4, 0x2BC])
    one(SITE_I, "offset", 0x416, 2, [0x41A, 0x41E])
    one(SITE_J, "offset", 0x430, 2, [0x434, 0x438])
    # a table ends where the next table begins
    one(SITE_E1, "pointer", 0x330, 2, [0x340, 0x344])
    one(SITE_E2, "pointer", 0x338, 2, [0x348, 0x34C])
    direct = tables_at(space, SITE_D)
    require(sorted(t["at"] for t in direct) == [0x2E0, 0x2E4] and
            all(t["kind"] == "direct" and t["complete"] for t in direct),
            "an address loaded on two paths is not two direct targets: %r" % direct)
    # code entered at a computed distance: its start is all that is known
    inline = tables_at(space, SITE_H)
    require(len(inline) == 1 and inline[0]["kind"] == "inline" and
            inline[0]["targets"] == [INLINE] and not inline[0]["complete"] and
            evidence_at(space, INLINE + 2) == "table",
            "an inline computed jump is not one incomplete target: %r" % inline)
    # an address from the stack, and a register a call may have changed, resolve nothing
    require(not tables_at(space, SITE_F) and not tables_at(space, SITE_G),
            "a transfer through an unknown register was given a table")
    require({u["at"] for u in space["unresolved"]} ==
            {SITE_A, SITE_B, SITE_C, SITE_D, SITE_E1, SITE_E2, SITE_F, SITE_G, SITE_H,
             SITE_I, SITE_J},
            "a computed transfer is no longer listed as one")
    entries = {e["at"]: e for e in space["entries"]}
    require(entries[0x264]["kinds"] == ["table"] and entries[0x264]["evidence"] == "table",
            "a table's target is not an entry of table evidence")
    require(not space["conflicts"] and not space["undecodable"],
            "table discovery left conflicts or undecodable entries")
    return space


def switched_off(rom):
    space = rom_document(rom, tables=False)
    require(space["tables"] == [] and evidence_at(space, HANDLERS["a1h"]) is None,
            "tables=False still follows tables")


def contradicted(rom):
    """A trace that saw the site go somewhere its table does not hold."""
    space = rom_document(rom, [Saw(SITE_A, HANDLERS["d0h"])])
    refused = tables_at(space, SITE_A, "refused")
    require(len(refused) == 1 and "observed target" in refused[0]["reason"] and
            not tables_at(space, SITE_A),
            "a table a trace contradicts was not refused: %r" % space["tables"])
    require(evidence_at(space, HANDLERS["a1h"]) is None,
            "a refused table's rows were still followed")
    # an incomplete table is not contradicted by a target it does not hold
    partial = rom_document(rom, [Saw(SITE_H, INLINE + 2)])
    require(tables_at(partial, SITE_H), "an incomplete table was refused by a further target")
    agreed = rom_document(rom, [Saw(SITE_A, HANDLERS["a2h"])])
    require(tables_at(agreed, SITE_A) and evidence_at(agreed, HANDLERS["a2h"]) == "traced" and
            evidence_at(agreed, HANDLERS["a1h"]) == "table",
            "a table a trace agrees with was not kept beside the traced target")


def through_intake(temp, rom_path):
    """The skeleton assembles table code and still rebuilds the image; the
    census counts it apart and the catalog gives it no tier."""
    counts, routines, rebuilt = intake.run_intake(rom_path, temp / "out", neutral_id="table-control")
    require(rebuilt, "the image with table code does not rebuild")
    rom_counts = next(space for space in counts["spaces"] if space["id"] == "rom")
    require(rom_counts["decoded_code_bytes"]["table"] == 17 * 4 + 2 * 4 + 4 + 6 and
            rom_counts["tables"] == 10 and rom_counts["unresolved_without_target"] == 2,
            "census table counts are wrong: %r" % rom_counts)
    tiers = {r["at"]: (r["tier"], r["evidence"]) for r in routines["routines"]}
    require(tiers[HANDLERS["a1h"]] == (None, "table"), "a table entry was given a tier")
    require("table" in intake.summary(counts), "the summary does not report table code")


def main():
    with tempfile.TemporaryDirectory(prefix="nmd-table-") as temp:
        temp = pathlib.Path(temp)
        rom_path = build_rom(temp / "rom")
        rom = rom_path.read_bytes()
        space = forms(rom)
        switched_off(rom)
        contradicted(rom)
        through_intake(temp, rom_path)
    accepted = [table for table in space["tables"] if table["status"] == "accepted"]
    print("table_check: ok (%d tables of 5 kinds give %d entries; a row that is not code, "
          "a neighbouring table and a first handler end a table; an unknown register resolves "
          "nothing; a contradicting trace refuses a table; rebuild identical)"
          % (len(accepted), len({t for table in accepted for t in table["targets"]})))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, OSError) as exc:
        sys.exit("table_check: " + str(exc))
