# vasm 1.9d provenance audit

Audit date: 2026-11-02. This checks the Mega Drive toolchain pin
`685a87e5ed14285350ccdb6581c9771bc8df6c7d` against the available source and
license evidence. It does not establish equivalence to an author-published
archive.

## Findings

The pinned [Git commit](https://github.com/Leffmann/vasm/commit/685a87e5ed14285350ccdb6581c9771bc8df6c7d)
and the `_VER` macro in [`vasm.c`](https://github.com/Leffmann/vasm/blob/685a87e5ed14285350ccdb6581c9771bc8df6c7d/vasm.c)
label the snapshot v1.9d.
The Leffmann project describes itself as Frank Wille's portable assembler and
links to the [official project site](https://sun.hasenbraten.de/vasm/). At audit
time, `git ls-remote --tags --heads` exposed only `master` at the pinned SHA and
no version tag. The commit is authored as Leffmann, not Frank Wille. These facts
identify a v1.9d-labelled mirror snapshot, but do not show that it is byte-for-byte
the author's release archive.

The pinned [`doc/vasm.texi`](https://github.com/Leffmann/vasm/blob/685a87e5ed14285350ccdb6581c9771bc8df6c7d/doc/vasm.texi)
states that vasm may be redistributed unmodified and used non-commercially; other
commercial use requires written consent, except M68k targeting AmigaOS. The
[current official manual](https://sun.hasenbraten.de/vasm/release/vasm.pdf)
states the same general terms, but is a 2025 manual, not the v1.9d archive. The
selected M68k, Motorola syntax, oldstyle syntax, and binary-output module notes
refer to vasm's main terms. The selected [Z80 module note](https://github.com/Leffmann/vasm/blob/685a87e5ed14285350ccdb6581c9771bc8df6c7d/doc/cpu_z80.texi)
contains a separate BSD-style notice that must also be preserved. This audit
does not interpret the terms for a particular commercial use.

## Archive comparison and remaining gate

The official site and a candidate `vasm.tar.gz` endpoint could not be retrieved
from this host during the audit; no author-published 1.9d archive or archive
digest was obtained. Therefore no normalized tree or file-by-file comparison
was possible, and exact archive equivalence remains unverified. The repository
builds vasm locally under `build/toolchains/` and does not ship it, but that does
not close the provenance question.

To close the gate, obtain the 1.9d source archive from Frank Wille or the
official site, record its SHA-256 and source URL, then compare its extracted file
paths and bytes against this pinned Git tree. Until then, retain the pin as a
mirror snapshot, preserve both the main and Z80 module notices, and keep the
archive-equivalence status explicitly open.
