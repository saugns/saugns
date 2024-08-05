saugns version changes
======================

[On the website](https://sau.frama.io/changes.html#saulang)
is a shorter change log with only the SAU language changes.

Pre-release
-----------

Added self-PM/"feedback FM". New `R` mode `a`.

Language changes:
 * Add `p.a` amplitude feedback parameter for phase,
   for phase self-modulation. Accepts both sweep and
   modulators within `[]`. Default value is 0.0.
 * Signal generator types. `R` mode `m` noise functions:
   - Add `a` (additive recurrence, low-discrepancy a.k.a.
     quasirandom sequence) by default based on the golden
     ratio. Add mode subparameter `m.a` for changing the
     multiplier used to the fractional part of a number.
   - Rename `r` (uniform random, default) to `u`.
 * Numerical expressions. Changes to mathematical functions:
   - Add `arbf(x)`, additive recurrence base frequency.
     Returns a multiplier for how much the pitch will
     change for an `R` instance when `x` is set to `R ma.a`.
     The value may be negative, corresponding to direction
     in a sawtooth-like wave which rises rather than falls.
   - Add `arhf(x)`, additive recurrence higher frequency.
     Like `arbf(x)`, but for the closest new frequency above
     the unshifted base frequency, instead of below it.
   - Add `sgn(x)`, which returns the sign of `x` as +/- 1
     or 0. (The sign bit is also preserved for 0.)
   - Remove deprecated `seed(x)` function replaced by `$seed`.
 * Seedable common parameters. Add `s` common to `N` and `R`,
   for overriding the default seed (assigned to new instances
   based on the `$seed` variable and a random sequence
   derived from it). `s` takes a value modulo 1.0 as the
   percentage of the state space, so `s0` means beginning, 0.

This self-PM support requries, to preserve the performance
when not using the feature, twice as much code for both the
`W` and `R` implementations. Self-modulation typically takes
at least 3 times more CPU time, sometimes more than 4 times.
(Self-modulation precludes use of vectorizing optimations.)

The parameter range uses the simplest scaling possible, like
multiplying the value by pi. This maps 1.0 to Yamaha feedback
level 6 in their chips. To avoid excessive ringing at that
level, stronger filtering than Yamaha used is used: 1-zero
(their choice) combined with 1-pole (itself a little better
at dampening self-oscillations than a 1-zero filter alone).

v0.4.4d (2024-07-10)
--------------------

Another minor parsing bugfix. Refactoring.

Fix parsing bugs for (still undocumented) `{}` grouping.
Refactor scope handling in parser, extend arrtype module.

"Random segments oscillator" has been renamed to
"Rumble oscillator, a.k.a. random line segments oscillator"
in the documentation.

v0.4.4c (2024-06-01)
--------------------

Minor parsing bugfix. Refactoring.

Language changes:
 * Nesting syntax.
    - Fix parsing of num. expr. namespace for "Y" in `[X][Y]`;
      for any "X" object params, "Y" sweep namespace was lost.
    - Change undocumented generic `<...>` nesting, to `{...}`.
 * Sweep syntax. Remove deprecated support for params in `{}`.

Rebased down modulator list `-` and concat, and sweep and list
unification, to v0.2.x (modified sgensys versions pre-saugns).
Logs kept while scripts in branch have one less syntax change.

Changes in naming and code style built up, working through the
versions. The new code is tidied similar to old rebasing work.
Outside the parser, refactoring and more cover some more code.

v0.4.4b (2024-04-16)
--------------------

Turn PRNG seed into cli-settable variable.

Language changes:
 * Numerical expressions. Replace `seed(x)` with `$seed` magic
   variable, which composes with the non-overriding assignment
   feature for simple cli setting and script handling of seed.
   [Add backward-compatibility deprecated `seed(x)` function.]

Improve usage warnings on failed `$?name`, `$?name=...` check.

Old scripts can usually be updated with one of the below regex
options -- the 2nd will use `?=` assignment to allow override.

`perl -pi -e "s/\\/seed/\\\$seed=/g;" paths...`
`perl -pi -e "s/\\/seed/\\\$seed\\?=/g;" paths...`

v0.4.4 (2024-04-09)
-------------------

Cli and variable feature expansion.

Command-line options:
 * Add "variable=value" option, to set variables and pass them
   to scripts. Each variable is passed as `$variable`, and the
   name must be valid for use as a SAU variable. Each value is
   restricted to only a number which may have a decimal point.
 * Make `-e` apply only to scripts after, not to every script.

Language changes:
 * Variable syntax.
    - Change numerical variable assignment syntax to use a `$`
      as leading character: `$name=...`, unlike object labels.
      [Add backward-compatibility deprecated alias, `'name=`.]
    - Labels (`'name `, `@name`) and variables (`$name`) won't
      conflict if they have the same name any longer. Now they
      are separate rather than one dynamically typed variable.
    - Add `?=` non-overriding numerical assignment; only takes
      effect for a variable that doesn't hold a number. Has no
      side effects when skipping evaluation of the expression.
    - Add `$?name` construct warning when a numerical variable
      isn't already set to a number. Can be used by itself, or
      combined with assignment (`$?name=...`) to also do `?=`.
      If used by itself, the script won't run on failed check.

The new cli option and SAU `?=` syntax work together, to allow
passing named, numerical arguments to a script. A script makes
predefining a value optional by using `?=` to set its default.
Using `$?name` requires that `name` be passed, used by itself.

Old scripts can be updated to use the newer assignment syntax,
without any manual adjustment, using regex search-and-replace:

`perl -pi -e "s/'([A-Za-z0-9_]+\\s*)=/\\$\\1=/g;" paths...`

v0.4.3 (2024-04-03)
-------------------

Add `N` noise generator. Small redesign steps.

Language changes:
 * Add generator type `N` (Noise generator), a plain
   noise generator without any frequency parameters.
   It has the underlying noise types of `R` and red.
 * Variable syntax. Relax rules for use of numerical
   variable `=`, allow whitespace after `=`, and not
   only before it. This behavior is less surprising.
 * Allow `[]` list unconnected to any parameter. Its
   objects are held unused. More language constructs
   are needed to use the free list, or its contents.
 * Amplitude handling. Add `S a.m`, script-wide gain
   mix control in outermost scope. Replaces the role
   of `S a` in disabling auto-downscaling by voices.

Parser redesign:
 * Turn lists into a main type, for two with the old
   audio "operator". Create objects, do type checks.
 * Make parseconv a utility header the parser simply
   uses. Simplify memory handling & code structures.

Fix some parser warnings wrongly numbering position.

v0.4.2d (2023-12-31)
--------------------

Various fixes. Beginning of new redesign.

Language changes:
 * Channel mixing. Fix combination of `c[]` modulation
   and non-swept non-center values; e.g. `cL[...]` now
   is at left compared to `cC[...]`, before it wasn't.

Fix minor bugs in parser timing code:
 * Make time `td` default time work properly
   for modulators; it (from v0.3.11c) always
   used to change to implicit time for them.
 * Fix bug causing default times to end up
   too short in a few situations; cleanup.

Parser redesign:
 * Make postparse code in parser run once
   per durgroup just after it, not at end
   of script. Adjust, simplify time code.
   Preparation for further time redesign.
 * Move voice number allocation to parser
   from parseconv. Don't fix a number for
   each carrier, allow their renumbering.
   This made voices number optimally low.

Add `CHANGELOG.md` based on tag messages and the website,
with a few additions for clarity on parts of the history.

A few scripts may now count as having fewer voices and
consequently play louder, if auto-scaling of amplitude
is used. One example being, `'a W cL| 'b W cR| @a t1`.

v0.4.2c (2023-10-02)
--------------------

Add quartertone pitch names, add JI systems.

Language changes:
 * Frequencies as notes.
    - Add flat `b` alias (`f` still supported).
    - Add half-flat `d` and half-sharp `z`.
    - Add flat-and-a-half `v` and sharp-and-a-half `k`.
    - Add double-flat `w` and double-sharp `x`.
    - Add `S f.s` tuning systems `c` (classic
      5-limit JI) and `p` (Pythagorean 3-limit JI).

Revise and extend frequency as note further.
Extend default 12TET-based options to 24TET;
use the 7-factor quartertone for JI tunings.

(JI is short for just intonation.)

Add a classic 5-limit JI `c` mode with sharp
and flat always the same size unlike in `j`.
Also add Pythagorean tuning system mode `p`.

v0.4.2b (2023-09-08)
--------------------

Allow `[X][Y]` to `[X Y]` concat.

Language changes:
 * Modulator list syntax. Writing two or more lists in
   direct succession now concatenates their contents.

Minor feature addition which is backward-compatible.

Other than that, this is a clean-up release, removing
obsolete and misleading looping in `generator.c`, and
trimming obsolete code in `parser.c`.

v0.4.2 (2023-08-23)
-------------------

Unify sweep syntax & reworked modulator list.

Language changes:
 * Sweep syntax. The subparameters are now enclosed in
   a `[]` list rather than in `{}`. They can be placed
   at the beginning of a list also used to assign
   modulators, for assigning both in one go.
 * Modulator list syntax. Assigning modulators in
   a `[]` list to a parameter now appends the new list,
   instead of replacing any old items with it. To clear
   old items instead of appending to them, a dash
   can be added before the `[`, as in `-[`.
 * Channel mixing. Add support for `c[]` panning-AM
   modulators, in addition to value sweeps. Produces
   a stereo placement AM-like effect that disappears
   on mono downmix.

Most older scripts did not remove modulators by setting
new lists; those that do will require adjustment. Apart
from that, a perl one-liner can be used to upgrade the
sweep syntax in scripts. Deprecated support for the old
sweep syntax is kept in the short term.

`perl -pi -e 's/{/\[/g; s/}/\]/g; s/\]\[/ /g;' paths...`

v0.4.1 (2023-07-04)
-------------------

Wave type overhaul. More line types, `R` modes.

Language changes:
 * Signal generator types. Add `R` mode `m` flags
   `h` (half-shape waveform, for sawtooth-like waves) and
   `z` (zig-zag flip, adds jaggedness with randomness).
 * Wave types. Rework into collection of `sin`,
   3 x 3 complementary types, and 2 extras.
    - Add `par` and `mto` -- with `saw`, all-harmonics trio.
    - Add `ean`, `cat`, and `eto` -- even-harmonics trio.
    - Rename `ahs` to `spa` (sine parabola),
      adjust phase offset to match other wave types.
    - Rename `hrs` to `hsi` (half-rectified sine),
      adjust phase offset to match other wave types.
    - Remove `ssr`, little-used all-harmonics wave type.
    - Make `saw` decreasing instead of increasing to match
      other types; flip amplitude or frequency for increasing.
 * Line types.
    - Add `sqe` (square polynomial envelope),
      a softer alternative to `xpe`.
    - Add `cub` (cubic polynomial segment),
      with steep ends and a flat middle.
 * Frequencies as notes.
   - Change tuning system to 12TET, adding a new `S f.s`
     toggle with the previous justly intoned scale as the
     other option (`j` value).
   - Don't treat `Cf4` as belonging to the octave above `C4`
     (2011 behavior), instead change default octave for `Cf`
     to 5 when using key `C`.
   - Add key-changing option `S f.k`, for handling of default
     and relative octave numbers (and rotating the microtonal
     small letter scale).
 * Variable syntax. Allow an optional letter for parameter
   namespace for an expression after numerical `=`,
   whitespace or a math symbol between it and math
   value/ID. Used to allow named constants for a paramater,
   for channel, frequency as note, or phase.
 * Nesting syntax. Re-add undocumented generic `<...>`,
   useful for wrapping around use of `S`. (Removed v0.3.12.)

Old scripts using `Wssr` will no longer work as expected;
if that wave type is wanted, it could be re-added later.
Most other old scripts will produce equivalent results after
adjusting `saw` uses, and adjusting phase `p` for and
renaming the old `hrs` and `ahs`.

v0.4.0d (2023-03-17)
--------------------

x86-64 clang builds fix. Add 2 line types.

Language changes:
 * Line types.
   - Add `nhl` (noise hump line)
     -- a medium line-plus-noise type.
   - Add `ncl` (noise camel line)
     -- a softer line-plus-noise type.

Fix clang x86-64 build bug, which broke audio
for `R mt` and `R mvb` in scripts in v0.4.0x.
Did not affect x86 clang, x86-64 gcc, builds.
Issue #4 on Codeberg has more information:
<https://codeberg.org/sau/saugns/issues/4>

Split out COPYING.LESSER from COPYING
so that automated license listings on
various websites work with this repo.

v0.4.0c (2023-02-24)
--------------------

Some further noise feature additions.

Language changes:
 * Signal generator types. Add `R` mode `m` flag `v`
   -- to select from a set of violet noise functions
   instead of white noise functions, when available.
   (Currently missing for the `g` and `t` functions.)
 * Line types. Add `uwh` (uniform white noise)
   -- makes a sweep use uniform random numbers
   in the start-to-goal range, and R produce chunks
   of noise with a random amplitude and DC offset
   each (if using the default mode).

Fix a minor parsing bug for unary sign without
surrounding parentheses, causing bogus warning
in scripts about missing whitespace afterward.

Add, update, replace some `R` example scripts.
The new versions match the new online examples
found at <https://sau.frama.io/examples.html>.

v0.4.0b (2023-02-06)
--------------------

Small usability tweaks.

Language changes:
 * Allow `R` without initial line type (default `cos`).
 * Allow `W` without initial wave type (default `sin`).

Change Makefile install and uninstall targets
to check whether a `share/examples` directory
exists; if not, place the example files under
`share/saugns/examples` (more common on Linux
distros) rather than `share/examples/saugns`.

Tweak cli code to use stdout, not stderr, for
`-h` (help) and `-V` (version) options.

v0.4.0 (2023-01-27)
-------------------

Add `R` random segments generator.

Language changes:
 * Rename generator type `O` to `W`.
   [Add backward-compatibility deprecated alias.]
 * Add generator type `R` (Random segments generator).
   It shares most parameters with type `W`.
 * Rename sweep subparameter `r` to `l` (line fill type).
   [Add backward-compatibility deprecated alias.]

Also, new example scripts actually worth
listening to (much more so than the old).

The design is minimally adjusted in this version;
the main thing is that there's now a full-featured
second audio generator available. More OO redesign
will be done for adding further generator types.

The `N` noise generator in the 2023-01 "mgensys"
version (`old-dev_202301` git branch) is still
left to bring into this main branch/program,
but everything else of value is available,
in a better, more polished program.

Note: The line and sweep features were earlier named
ramp (ramp curve, ramp syntax), before this version.

v0.3.12 (2023-01-01)
--------------------

Fixes. New SAU features. Split lib.

On 32-bit x86 systems, fix PM bug: use llrintf() on
phase values meant to wrap around, not lrintf(), to
make large phase values work properly. This audible
2018 bug embarrassingly goes back to saugns v0.3.0.
Didn't affect any 64-bit platform with 64-bit long.

Also fix possible crashes for some odd sample rates
in some cases, and small divergences in the output.
Was due to an unrelated error in time/buffer logic,
which however appeared with the same v0.3.0 change.

This change splits out a static library under sau/.
Some code, e.g. the old ptrarr module, was deleted.

Add cli -d deterministic option; makes time() zero.

AM/RM and FM.
 * Support lists directly under `a`, `f`, and `r`,
   as in `a[]` (AM/RM) and `f[]`, `r[]` (FM).
   Modulators in such lists have the output added
   directly to the parameter value. Can be used
   together with the older value range feature,
   and the effect is then added on top of that.

Script options.
 * Make the `S` options lexically scoped,
   restoring the older values from outside
   a `[]` list when the list is exited.
 * Apply the `S a` amplitude multiplier set
   inside a `[]` list to any modulators which
   follow at the same list level. Previously
   it was only applied to top-level carriers.
   Separate this particular setting at each list
   level, making the multipliers independent,
   except apply it to sublists of the new `a[]`
   variety.
 * Rename `S` option `n` to `f.n`.

Ramp syntax.
 * Rename ramp `hold` to `sah` (sample and hold).
 * Rename ramp `sin` back to `cos`.

Variable syntax.
 * Add `'name=...` variation of the syntax for
   variable assignment, for assigning a number.
   Variables are now dynamically typed. (The old
   and other value type is reference to object.)
 * Add `$name` expression, for using a variable
   in a numerical expression.

Numerical expressions.
 * In numbers, if a decimal `.` is used, require
   digit(s) after. A trailing 0 can no longer be
   skipped but a leading 0 can still be skipped.
   (E.g. 0, 0.0, and .0 are all fine, 0. isn't.)

Subnames.
 * Modulation with value range now uses e.g.
   `a.r`, `f.r`, rather than `a,w`, `f,w`.
 * Frequency-amplified PM uses `p.f`, was `p,f`.

v0.3.11c (2022-07-31)
---------------------

More flexible value ramp usage.

Syntax changes:
 * Add special value for main time `t`
   parameter with literal `d` in place
   of the `*` feature v0.3.10 removed.
 * Ramp `{...}` and its subparameters:
   - Allow ordinary value, and/or subvalues in `{...}`,
     and/or modulator list, in one go, where available.
     They can only be written in that particular order.
   - Add `v` (start value), as alias for the ordinary
     value before the `{}`; allow use of one of them.

Allow setting just any one subvalue, or any combination,
within `{...}`. Changing goal before old goal is reached
now updates the start/ordinary value to the point reached
on the prior trajectory. What remains of an unexpired time
set for a ramp will now always be the default for its time
until it runs out. And a ramp shape set is kept for any
new updates for the parameter.

Also includes a very small audio generation performance
improvement, and an expansion of symtab code for later.

Add -v verbose option, rename version to -V.
Currently it prints which script is playing.

Update documentation for website move to
<https://sau.frama.io>. (The older pages
redirect to the newer.)

v0.3.11b (2022-06-28)
---------------------

Further syntax tweaks. Small fixes.

Syntax changes:
 * For modulation with value range, change delimiters
   between the parts from `Xw,Y[...]` to `X,wY[...]`.
 * For frequency-amplified PM, `,` added before `f[`.
 * Allow numerical expressions to omit `*` after `)`,
   not only before `(`, for shorthand multiplication.

There is no longer any need to sometimes place named
constants in parentheses to separate them from added
`w` or `f` after, as `,` is now placed between them.

Reduce rounding error for numbers read from scripts.
Precision improved from a little better than single,
to just below double. Practically, the difference is
small, since output from the parser is still reduced
to single precision after the numerical expressions.

v0.3.11 (2022-06-15)
--------------------

Fixes, cli features, syntax tweaks & more.

Adjust default time behavior, document lengthening
based on time of nested objects. Some smaller bugs
remain, a further redesign needed. But fix two for
modulation lists combined with timing syntax (file
`devtests/defaulttime3.sau`). Previously durations
could lengthen past the play time based on that in
nested lists; also, for `;`-separated (numberless,
older syntax) sub-steps, gaps mistakenly produced.

This also includes a bugfix for a timing
issue added in v0.3.10b. A one-line bug,
in a new time conversion function (added
close to that release) made longer times
roll over and become too short. (Example
scripts had too short `t`s to catch it.)

Syntax changes:
 * Rename `\` with number to `;` with number. The same use.
 * Newlines in the top scope will no longer end the current
   step, handling now becoming more regular and permissive.
   (Previously, an exception to the rule was new sub-steps,
   allowed on the line after a linebreak. `\` was different
   and not included in that exception. Now top and subscope
   handling is more similar and should be less surprising.)
 * For modulation with value range, change delimiters
   between the parts from `X,Y~[...]` to `Xw,Y[...]`.
 * For phase modulation (normal, frequency-amplified)
   remove `+` after `p` and place to set phase value.
 * Ramp `{...}` sub-parameters:
    - Rename `v` to `g` (goal).
    - Rename `c` to `r` (ramp).
 * Rename ramp `cos` to `sin` (sinuous curve). It's
   maybe more intuitive. Also use a new polynomial,
   producing a reasonably high-fidelity sine curve.
 * Numerical expressions:
    - Allow number signs and arithmetic operations
      (full expressions) outside parentheses, with
      any whitespace ending the expression. Undoes
      a change from v0.3.0, restoring old feature.
    - Make `^` right-associative.
    - Add `%` remainder parsed similar to division.
    - Add functions:
       + sin(x), cos(x)
       + rand(), seed(x), time()
       + rint(x)
    - Add constants:
       + mf, pi

Old scripts can be converted to the new syntax
using in part simple search-and-replace steps:
 1. Each old `\` should become `;`.
 2. Each `,` should become `w,`; if
    there's a new `w,` before a `~`
    the `~` should be removed, else
    it should be replaced with `w`.
 3. Each `+[` should become `[` and
    every `+f[` should become `f[`.
 4. Each `v` inside a `{...}`
    should be changed to `g`.
 5. For ramp arguments `cexp`, `clog`, etc.,
    replace string with `rexp`, `rlog`, etc.
    Except for `ccos`, which becomes `rsin`.

A smaller change included is that the `O f`
and `S n` default values are now 440. Seems
less awkward than the older, whimsical 444.

Add `--mono` and `--stdout` cli options.
Also support `-o -` audio to stdout with
AU format header. (Unlike WAV, AU always
allows headers without a known length. A
file written with a name (not using `-`)
is still written as a WAV file however.)

v0.3.10b (2022-02-23)
---------------------

Various fixes. Add freqlinkPM syntax.

Fixes:
 * Audio: The v0.3.9 oscillator produced jagged shapes
   and audible distortion when PM modulator amplitudes
   were large. Fixed but made generation a bit slower.
 * Numerical expressions: Fixed parsing of unary minus
   several times in a row and in combination with `^`.
 * Timing syntax: Correct handling of `\` after `;` in
   compound steps for modulator operators. Re-set time
   using the last default value, not the last explicit
   `t` value. Makes implicit time handling consistent.

Syntax changes:
 * Add new `p+f[...]` syntax, for frequency-linked PM.
   Multiplies the amplitude of modulators with carrier
   frequency scaled so that 632.45... Hz has 1.0 gain.
   Based on an idea I remember from 2011, before I got
   normal modulation in order, now seeming worthwhile.

Rework some scripts to show the use of the new syntax.

Clarify documentation a little bit more, regarding FM.

v0.3.10 (2022-02-03)
--------------------

Timing fixes. Rework timing modifiers.

Changes of a few main varieties:
 1. Fix compound step syntax used with nesting.
 2. Fix some smaller timing-related bugs.
 3. Rework timing modifiers, replace `s` (silence) parameter.
 4. A few smaller syntax removals, and an addition.

Fix a bug (design flaw) from 2011, which limited use
of `;` for timing in a nested structure in a script.

Previously, while scripts with `Osin p+[Osin; ...]`
worked, scripts with `Osin p+[Osin; ... Osin; ...]`
didn't. The bug affected timing, when more than one
operator inside or after nesting rather than before
nesting uses `;`. Now that limitation is gone.

New `devtests/compnest.sau` script works now but not
with earlier versions. (Note: The fix was backported
all the way down to new "Debug compound..." commit.)

Fixes for `-p` option duration
reporting, and related things.

This is the 3rd year in a row where timing bugs have
been fixed in December-January, for whatever reason.

Syntax changes:
 * Rename timing modifier `\` to `/`,
   (reverses change from 2011-07-01).
 * Replace the old silent time padding `s` parameter
   with the new subshift `\` timing modifier, also a
   new feature (add delay only for a next sub-step).
 * Make `|` reset delay to add next step to duration
   rather than add it to delay from prior `/` usage.
 * Remove "default length time" (`t*`, literal `*`),
   a so-far never-useful 2011 feature.
 * Remove "delay by previous time" (used to be `\t`,
   literal `t`), another 2011 feature.
 * Add phase named constant `G` for golden angle, scaled
   to cycle percentage, for use in e.g. `p(G*4)` for the
   4th leaf-around-a-stem angle.

Some smaller design changes without change in features.

Make nicer README (now `README.md`), also clear in plaintext.
Also, rewrite various parts of `doc/README.SAU`, for clarity.

When adjusting scripts to use `\` instead of `s`
(after renaming every `\` to `/`) make sure that
the `\` is always before any old `t` that is for
the same (sub-)step. Placement is now important.

As for old `\` followed by `|`, swap and write a
`|` before the new `/`, to get the old behavior.

v0.3.9 (2021-11-11)
-------------------

Anti-aliasing. Wave, ramp, math changes. Fixes.

Correct some irregularities in syntax handling
including most noticeably the O `c` parameter,
now behaving like other parameters (instead of
the last value set to an object on a line with
several objects being applied to all objects).

Rework oscillator for some real anti-aliasing.
Remove old "rounding" from `sqr`, `saw` waves,
use one level of DPW-like pre-integrated table
for all wave types (and FM, PM) in oscillator.

Extend num. expr. syntax, recognizing built-in
math functions which are now usable as part of
specifying arguments for numerical parameters.

Rework design from an earlier version and peel
off some complexity without functional change.

Remove new parseconv, rename scriptconv to it.

Correct and expand README on amplitude modulation;
describe ring modulation, also supported using the
AM syntax since early 2011 versions.

Wave types:
 * Rename:
    - `sha` -> `ahs` (absolute half-frequency sine).
    - `szh` -> `hrs` (half-rectified sine).
    - `ssr` -> `srs` (square root of sine).
 * Add:
    - `ssr` (squared & square root of sine).
 * Change:
    - `saw` (increasing instead of decreasing).

Ramp types:
 * Rename:
    - `esd` -> `xpe` (eXPonential Envelope).
    - `lsd` -> `lge` (LoGarithmic Envelope).
 * Add:
    - `cos` (cos-like increase or decrease).

Math functions:
 * Add:
    - `abs(x)`  Absolute value.
    - `exp(x)`  Base-e exponential value.
    - `log(x)`  Natural logarithmic value.
    - `sqrt(x)` Square root.
    - `met(x)`  Metallic value.

Also add test script for using pre-existing features
to get an "IXA synthesis" sound.

v0.3.8b (2021-01-23)
--------------------

Portability, performance, and cli fixes.

OSS on NetBSD: Use /dev/audio, not /dev/sound.

Check for the AUDIODEV environment variable,
using it to override the default device name
regardless of which type (ALSA, OSS, ...) is
used. Similar to handling in other software,
e.g. SoX and SDL. Empty strings are ignored.
Credit to Art Nikpal (@hyphop at GitHub) for
the basic idea, and SoX code as a reference.
Further refactor player/audiodev code a bit.

Change cli argument parsing to allow a flag to be followed
by its argument without a space in-between. Recognize `--`
as meaning no further strings are flags. Uses `SAU_getopt()`
derived from Christopher Wellons's public domain getopt().

Makefile changes to selectively use -O3.
Following benchmarking (repeated running
of example/ scripts with -m to time it),
this can have a benefit anywhere from no
to more than 42%, depending on compiler,
etc. (It seems to matter more with GCC.)

Move ramp mulbuf use into the fill functions
and adjust more, allowing mulbuf to be NULL.

Change the mempool to use dynamic sizing of memory blocks,
doubling size when the number of blocks exceeds a power of
four. Use 2048 as the start block size. Doesn't change the
performance significantly with my testing which became too
rough for tiny differences or needs larger volumes of use.

[The mempool was changed in next version, to doubling size
after each power of two, starting at 512 as default size.]

v0.3.8 (2020-12-29)
-------------------

Clean-up redesign. Fix timing bugs.

Most commits near the top, after the renaming to 'saugns',
have been replaced by expanded commits a bit further down.
Superficial stylistic changes have been rolled back a bit,
while the valuable redesign parts have been streamlined in
redone versions.

To make space for expanding list syntax, undocumented `[]`
usage with few effects as a generic subscope is renamed to
`<>`. The latter could be given a real semantics later on.

Furthermore, the
parser has been simplified with a new round of changes and
default time durations in scripts debugged. The design now
looks a bit more like 2020-06 "mgensys", in part. Next up,
preparing to expand language features on a cleaner ground.

Changes the fix for -r 1 hangs. Now data is generated, and
this extreme case also tested within the interpreter code.

v0.3.7c (2020-11-27)
--------------------

Rearrange code. Fix hang with `-r 1`.

Reduce design difference with 2020-06 "mgensys" a bit further;
split "generator" pre-allocation code into prealloc (like a
simpler runalloc), and merge voicegraph into prealloc. Also
rename generator to interp.

Fix hang with the useless, yet allowed, sample rate of 1 Hz
due to 0 samples being generated per "run" call, and time thus
never advancing for the interpreter. Simply end when 0 samples
are filled in the player code.

v0.3.7b (2020-11-14)
--------------------

Tweak cli and minor clean-up.

Command-line behavior: Print -p info for each script just prior
to generating audio for it if done, instead of printing for all
before generating for all.

Fix v0.3.6f slip-up in moving changes across branches, which
made the usage notice say "mgensys" instead of "saugns".

Flense generator, simplifying away most "event data"
and using what's allocated at the previous stage.

The code previously in renderer/ is now in player/.

v0.3.7 (2020-10-22)
-------------------

New channel mixing (panning) syntax.

The old P "keyword" for panning has been replaced.
Note that more changes remain to be done to remove
the old behavior of grouping the parameter setting
for all operators on one line. Place each on a new
line in the meantime to give it its own value.

Syntax changes:
 * Replace panning `P` keyword with channel mixing parameter
   `c` and `S c` for default. The scale changes from 0.0-1.0
   left-right to (-1.0) left, 0.0 center, 1.0 right. And the
   letters `C`, `L`, and `R` become named constants for 0.0,
   (-1.0), and 1.0.

Some additional refactoring is done,
for some more lower-hanging fruit in
ideas from the 2020-06 "mgensys" redesign version.

The commit sequence is expanded a bit further down
for further work toward the redesign
goals, with notes on remaining bugs.

v0.3.6f (2020-07-27)
--------------------

Reduce diff for upcoming redesign.

Reduce differences for various smaller modules with
the 2020-06 "mgensys" redesign experiment now in the
`old-dev_202006` branch. Relicense many small modules
permissively (ISC license), while saugns as a whole is
still LGPL'd.

Change default sample rate to 96000 Hz,
undoing the 2012-02-10 change to 44100 Hz.

Change the -h option to list topics
available with -h <topic>. For now,
only other lists are available, the
first built-in help consisting of a
'wave' type list and a 'ramp' list.

Rename `loader/` to `reader/`.

Apart from the mentioned changes to
options, should be functionally the
same as v0.3.6e.

v0.3.6e-2 (2020-02-09)
----------------------

(Re-tag.) Bugfixes, minor clean-up.

This replaces the hasty v0.3.6e tag. Back to `-std=c99`,
`-pedantic` removed, but the warning fixes included.
(The saugns `-v` version is also now increased.)

Fix crash on opening several files
when only some failed to open.

Fix numerical expression parsing for
several subtractions in a row.

Simplify usage notice, update man page.

(These changes are part of work included
in an experimental redesign, in branches
named beginning with mgs. Far from done,
but will be the basis for new versions.)

v0.3.6e (2020-01-05)
--------------------

Use `-std=c11`. Fix `-pedantic` clang warnings.

Removes new warnings when building with clang 8.0,
which appeared in previous version
(where `-pedantic` was added in Makefile).

No functional change.

v0.3.6d (2020-01-04)
--------------------

Fix crash in generator. Clean-up changes.

Fix crashes due to wrong-sized allocation in generator.
This bug was added in v0.3.1, and not found for 5 months
because things happened to work on my systems and I've
not received any feedback from users.

Also contains a smattering of clean-up changes.

v0.3.6c (2019-12-31)
--------------------

Refactoring.

Split out voicegraph from scriptconv,
add and use mpmemdup for arrtype uses
of memdup. Functionally the same, but
tidier for future redesign.

v0.3.6b (2019-12-30)
--------------------

Redesign. Fix v0.3.6 modulator list bug.

Extend the v0.3.4-v0.3.6 redesigns further,
using a common nodelist module for parser
and parseconv output, and moving handling
of specific modulator lists to scriptconv.

Fix v0.3.6 bug introduced when removing ADJCS flag.
Ensure that cleared modulator lists are produced
and set when clearing modulator lists in script.

Add `devtests/pm-addremaddrem.sau` (which
tests adding and clearing PM modulators).

v0.3.6 (2019-12-27)
-------------------

Redesign. Fix for undocumented feature.

Make parser handling of operator sublists simpler
and more generic, moving specifics to parseconv.

Refactor scriptconv and program.h types.

Make use of `[]` separately from list parameters
function as a generic subscope with few effects.
This feature is still undocumented. Mostly harmless.
(Originally, as `<>`, it simply allowed freer use
of whitespace. This was broken by the time of the
2012 releases.)

v0.3.5c (2019-12-22)
--------------------

Fix crash on updating ignored operator, timing.

Fixes:
 * Make parseconv ignore updates for operator nodes
   which weren't processed before, because the list
   in which they were created was cleared in the same
   event. Fixes crash in versions v0.3.5 and v0.3.5b.
   (Add `devtests/ref-unused_node.sau` for testing.)
 * Add missing default time handling for
   an operator's second amp. and freq. ramps.
   (Rebased down to v0.2.13.)
 * Fix a case where the first part of a composite event
   is given infinite time by default, messing up script
   timing (negative never-ending duration, event order).
   (Rebased down to v0.2.3.)

(The crash also happened with v0.3.4, but not
v0.3.3 and v0.3.4b. Versions earlier than v0.3.3
ran, but changed data for the wrong operator.)

v0.3.5b (2019-12-21)
--------------------

Fixes for `S a` and `@label` syntax.

Fixes:
 * `S a` (Set amplitude multiplier):
   Apply multiplier to amplitude ramp target,
   not only the normal or initial value.
   (This was missed a long time ago.)
   Rebased down to v0.2.3.
 * Change which flags are checked to
   allow/disallow `r` (rel. freq.) and `i` (inf. time),
   fixing syntax for `@label` references.
   Rebased down to v0.3.1d.

Also change op list code to include pointers instead
of direct instances for modulator lists in parse data.

v0.3.5 (2019-12-15)
-------------------

Refactoring. Reorganize code. Makefile tweak.

Various little code clean-ups. Reorganize source files.
Separate flags used during parsing from flags
assigned in resulting script data.
No functional change.

Handle whether or not to gzip man page
upon install (always done before),
according to whether the install location
or `/usr/share/man` (takes priority if found)
contains gzipped man pages.

v0.3.4b (2019-12-04)
--------------------

Parser refactoring, part 2.

Simplify memory handling by using the mempool
for all parse data node allocation. Freeing
becomes trivial. Also include the symtab in
the produced parse (since it depends on the
mempool), which may suit future needs.

v0.3.4 (2019-12-02)
-------------------

Reorganize scripts. Parser refactoring.

Move some scripts and snippets from devtests/.
Add examples/sounds/, further reorganizing a bit.

Parser refactoring in preparation for further redesign,
without change in functionality.

v0.3.3 (2019-10-28)
-------------------

Wave type changes. Internal reworking.

Wave type changes (rebased down to v0.2.14):
 * Use an anti-aliased cycle for the `sqr`
   LUT, removing low-frequency noise, but
   still a bit rough; the new sound lacks
   full "bite" at low frequencies.
 * Do the same for the `saw` LUT, with
   the same quality improvement (but a
   bit more "bite" preserved). The slope
   is now decreasing instead of increasing
   (can be changed with neg. amp. or freq.).
 * Remove `shh` type
   (very similar to old `saw`).

Internal design change in preparation
for future work, without functional change.

Rename parseconv to scriptconv,
adding a new parseconv layer after parsing,
in-between the two.

v0.3.2b (2019-09-14)
--------------------

WAV out fix. Add man page. Split README.

Added bugfix (rebased down to v0.2.3):
 * WAV file output:
   Correct byte length in header,
   was channel-count times too large.
   (Avoid double multiplication.)

v0.3.2 (2019-08-24)
-------------------

Replace old `#`, `Q` syntax.

v0.3.1c (2019-08-23)
--------------------

Some code tidying without functional change.

v0.3.1b (2019-08-06)
--------------------

Fix parsing when -ffast-math breaks isnan().

v0.3.1 (2019-08-03)
-------------------

AM/FM/PM syntax changes. Parser fixes.

v0.3.0 (2019-07-04)
-------------------

First release under the saugns name.

Support multiple script files or strings
per command-line invocation. The `-e` option
allows a series of strings to each be treated
as one script to evaluate.

Add sndio support; fixes for portability.

Nesting syntax.
 * List of operators and steps for them.
   From inside `<...>`, change to inside `[...]`.
 * Compound value (e.g. ramp arguments).
   From inside `[...]`, change to inside `{...}`.
 * PM modulator list prefix. From `p!`, change to `p+`.

Label syntax.
 * Names can now only contain alphanumeric characters and `_`.
 * Referencing syntax `:name` changed to `@name`.

Numerical expressions.
 * Only allow arithmetic and number sign within parentheses.
   (This went along with big fixes and more changes.)

Amplitude handling.
 * Add downscaling of output amplitude by voice count,
   enabled by default. Using `S a` disables it.
 * Allow `a` (amplitude) parameter for AM and FM modulators too.

Disable multiplicative inverse for `r` (relative frequency).

Ramp types.
 * Renamed `exp` to `esd`, and `log` to `lsd`.
 * Added changed `exp`, `log`, and new `hold`.

Wave types.
 * Renamed `srs` to `ssr`.
 * Added `sha`, `szh`, `shh`.

Comment syntax.
 * Also recognize C-style `/*...*/` and C++-style `//` comments.

sgensys 2013-03-04
------------------

Add ALSA support, `-c` command-line option.

sgensys 2012-04-01
------------------

2nd release at Gna! (sgensys-20120401.tgz)

Work on layer between parser and generator.

sgensys 2012-03-05
------------------

1st release at Gna! (sgensys-20120305.tgz)
