Further design ideas
====================

Syntax for multiple carriers sharing modulator etc. (2024)
----------------------------------------------------------

The old unimplemented "multiple object binding" syntax which was
never actually implemented, redone. Maybe e.g. `<carriers> p[modulators]`
for PM, also allowing setting other parameters for several objects in one
go. Another variation could be e.g. `<carr p carr p>[mods]`, where the
parameters (the two `p`) are bound rather than the objects they belong to.
Furthermore, the use of `<A...><B...>` could concat into `<A... B...>`.

Copy a labeled object using syntax `:label`, after `'label ...`. Can be
used to insert a carrier several times, duplicating the link from the
original carrier to any modulator(s) in the process.

Copy-insertion of objects in labeled list works similarly, at the level of
objects ultimately in use after.

Long-tailed envelopes, object cloning, and generator allocation (2025)
----------------------------------------------------------------------

The 2025 ADSR envelope feature, when combined with `;` substeps, limits
time of each note so the "tail end" of an envelope can't extend past the
beginning of the next note/substep. To extend it past, layering earlier
and later notes, manual use of several generators in scripts is needed.

To avoid copy-pasting generators in scripts and allow extending ADSR
release (and more) past the beginning of the next note with a simple `;`
separator for notes, automatic generator cloning could be implemented.
Just like the "Copy a labeled object" idea, it needs to take modulators
into account too -- reusing audio data and/or deep cloning the modulator
chains. Technically the two clone ideas share most details -- mainly
timing logic tied to envelope use vs. new syntax distinguish them.

A much older "automatic cloning" idea (2012 notes) for panning/channel
mix control allowing it to be set for modulators, not only top-level
carriers, is somewhat similar. What it shares is the "deep cloning" part
and the optimization of reusing audio instead for inner modulators when
possible. The difference is how clones are used and decisions are made.

For envelope-tied automatic cloning, there are two durations per event:
the short (when the next `;` step/follow-on event arrives) and the long
(the longest note + envelope duration). Thus, do the clone substitution
for the old event when the `;` step arrives, plus update as usual for the
new after. Timing logic like that for voice allocation (same algo) can be
reused to determine how many clones are needed in a chain of `;` steps,
reusing "expired" ones when possible.

The more general problem of "generator allocation" (not done before) to
have a minimal number of objects and IDs in use per script, can be solved
without needing an extra pass the same way one-pass allocation for voices
work, by allowing greedy re-allocation (i.e. renumbering) of generators.
