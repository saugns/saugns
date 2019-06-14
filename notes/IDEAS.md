Further design ideas
====================

Split into library and command-line player (2019)
-------------------------------------------------

Handling of scripts is suitably fast and easy on memory
for turning the code for the scripting language into a
library, used e.g. in input plugins for media players,
or in retro-styled games.

The player/ directory now contains code for a cli player
while the code for reading, preparing, and interpreting,
could be turned into such a library.

Passing arguments to scripts (2014)
-----------------------------------

Add argument passing to scripts.
Add retrieving e.g. "arg" array inside scripts.
Tests (branches), conditional actions...

Can allow e.g. a simple timer script that is configurable.

(Only support bounded lengths for iteration if implemented,
perhaps unless a special value is passed to the script to
keep it looping. Except for such invocation-controlled
modifiers, the language will be designed for pre-calculated
finite timing prior to interpretation, not Turing complete.
Arguments unpassed would get default values. Added in 2019.)
