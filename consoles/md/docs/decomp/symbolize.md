# Symbolic control flow (D08, first step)

`consoles/md/tools/decomp/symbolize.py` turns the decoded skeleton
([intake](intake.md)) into a listing whose branches, calls and jumps name
their targets:

```
symbolize.py --skeleton DIR --codemap codemap.json --out DIR \
             [--names names.json] [--baserom ROM]
```

Every direct transfer whose target is assembled as code gets a label for it:
`sub_XXXXXX` at a routine entry, `loc_XXXXXX` elsewhere, or the name
`--names` gives the address (`{"$5D2A": "draw_text"}`; a title's names stay
in its private workspace). Raw spans keep their addresses: a label goes only
where an instruction of the listing begins, and a transfer into a raw span,
or a computed one, is left as written. Z80 program listings are done the
same way.

The listing carries no addresses, so they are recovered by walking it:
`org` sets the address, an incbin or byte line advances it by its length,
and an instruction line must be the code map's instruction at the running
address and advances by its length. A line that does not line up stops the
tool.

With `--baserom` the copy is rebuilt (`skeleton.rebuild`) and must be the
image byte for byte. The assembler runs with `-no-opt`, so a named branch
keeps its written size.

## Evidence

`symbolize_check.py`, on the per-call control cartridge: 45 labels and 41
operands named, routines called by name, the rebuild identical, no transfer
to code left with a raw address; two planted errors (each label two bytes
late; an operand naming the instruction after its target) break the
rebuild.

First title: 1,013 labels and 1,570 operands in the 68000 listing (23
routines by their names), 14 labels and 17 operands in the Z80 programs;
the copy rebuilds the image byte for byte.

## Limits

Control flow only. Data references (`lea ($268).l,a0`), RAM and hardware
addresses, jump-table rows and comments are D08's remaining steps; code the
census has not decoded stays a raw span.
