# Contract discipline

This is how nativekit changes, and the standards every console in this repository keeps.
A console's game contract is its own (its headers, its document, its version); the kit
is the contract between the kit and its consoles. Both change by the same rules.

## How a contract changes

- **Major version:** the only kind of release that may change or remove anything a user
  of the contract writes or relies on. For a console's contract the user is a game; for
  the kit's, a console.
- **Minor version:** may only add things: a new call, a new header, a new setting, or a
  new field at the end of a struct that a zeroed struct leaves off. Everything written
  against the version before builds and runs the same.
- **Patch version:** a fix. For a console, it brings the console closer to the hardware
  where it differed from its reference: the contract is the hardware's behaviour, so
  this is a fix, not a change of contract. For the kit, it makes the kit do what its
  headers already say.
- Each contract states its version where a build can read it (a console in its own
  header; the kit in [`VERSION`](../VERSION), read by its CMake as `NK_VERSION`).

A console's contract does not move when the kit does: a console built on a new kit keeps
its own version unless what its games see changes, and then by these rules.

## nativekit's version

nativekit is at **0.1.0**. Until a second console builds on it, it is internal to the
consoles in this repository, and a minor version may still change what a console writes,
with each console moved in the same change. It reaches **1.0** when the second console,
the Mega Drive, builds on it unchanged; from 1.0 the rules above hold in full.

What is in the kit's contract: the headers in [`include/nk/`](../include/nk/) and the
settings and arguments [kit.md](kit.md) lists. Everything under `src/`, `tests/` and
`tools/` may change in any version, as long as the headers' promises hold.

## What every console keeps

- **Behaviour exact against a pinned reference.** Each console names its reference (an
  implementation held to be the hardware, at a pinned version with its own build) and is
  held to it read for read, pixel for pixel and sample for sample, by checks that run
  both. Where the console differs from the reference, that is a defect in the console,
  fixed in a patch release, or a difference written down with its reason in the
  console's list of known differences, which its checks keep true (a listed difference
  that starts matching is reported).
- **A fatal error, never a guess.** When a game reaches something its contract does not
  cover (an address with no C, a wait that cannot end, a request outside what the
  hardware has), the console says what and where, then exits with status 70 (`nk_fatal`).
  There is no fallback that runs on and might be wrong.
- **Extensions off until asked for.** A console's numbers are its baseline: every
  extension beyond the hardware (a larger screen, more memory, no limits, a pace
  independent of speed) is off until the game asks for it, and a game that asks for none
  runs on the console exactly.
- **The same run on every host.** One thread, a deterministic schedule (coroutines, not
  threads): the same input gives the same machine, on every operating system,
  architecture and renderer, headless or in a window.
- **Checks shown able to fail before they are relied on.** Every claim a console or the
  kit makes is held by a check, and each check was shown to catch a planted error before
  it was trusted. A check that cannot fail holds nothing.
