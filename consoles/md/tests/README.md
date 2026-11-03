# Mega Drive Test Fixtures

Oracle tests assemble synthetic cartridges and generate their fixture data at
runtime. No commercial ROM images are stored in this directory. Local library
surveys accept the ROM directory as a runtime argument; reports contain
sanitized titles and observations, not ROM paths or contents.

BlastEm and vasm are local test dependencies built under `build/toolchains/`;
they are not shipped with nativekit. Review the pinned revisions, patch
provenance, and license notes in [`tools/toolchains/README.md`](../../../tools/toolchains/README.md)
before redistributing or using those tools. The vasm setup is limited to
non-commercial use, and exact equivalence between the pinned mirror and an
author-published archive remains unverified.

The candidate BlastEm traces and generated reports are exploratory evidence;
they do not qualify hardware accuracy or title compatibility.

Run `mise run test:md-bus` for native bus checks, `mise run test:md-interrupt`
for scheduler and interrupt checks, and `mise run test:md-rom` for shared
synthetic bus operations on BlastEm and nmd. The interrupt fixture compares
HINT line and relative-clock deltas because the candidate trace epoch starts
at a different absolute clock. Candidate-only differences stay in `rom/KNOWN`.
