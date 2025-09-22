Further design ideas
====================

Replace parseconv, redo processing passes/stages (2024)
-------------------------------------------------------

* Merge parseconv middle layer into parser layer (top) and/or
  generator/interpreter layer (bottom). Flatten away passes,
  loops dealing with representations of script data as much as
  possible. Convert and rework stuff without looping through the
  whole thing more times than at most 2 (parse + run), ideally.
* Rework node datatypes (and format for data contained in nodes)
  used *prior to* the gen/interp layer however is most elegant
  while efficient enough. Avoid unnecessary copying between
  representations, do such changes of format only where it
  simplifies algorithms after.
* New parseconv pseudo-layer: Nested calls in parser's node run
  pass converts to new audio "program" format, with smaller
  instructions which the generator/interp end then runs. Such
  new instructions have block/buffer scheduling along with them,
  more optimized resource use, make for more flexible, general
  code in gen/in with D.R.Y. as more stuff is supported.
* Printing and list ID array stuff goes before this new parseconv
  conversion, or along with it using the representation which is
  its input.
* "New parseconv" can track meta-objects, that is objects which
  are script data used as templates or otherwise as part of
  features for eventually making other objects which go on to be
  used in signal generation.
* The features of "new parseconv" may warrant it ending up like
  an earlier interpreter layer (running inside the parser pass),
  producing stuff for the then-later new-old interpreter layer
  (the generating or rendering layer).
* Buffer scheduling/reuse in gen.interp. can, per event reset the
  usage flag for all resources used, then per fill run flip a bit
  making it the color of the run (then toggle resources when used
  if not already toggled, else knowing to reuse results instead).

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
