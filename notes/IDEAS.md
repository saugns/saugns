Further design ideas
====================

Passing arguments to scripts (2014)
-----------------------------------

Add argument passing to scripts.
Add retrieving e.g. "arg" array inside scripts.
Tests (branches), conditional actions...

Can allow e.g. a simple timer script that is configurable.

(2019 thoughts: Only support bounded lengths for iteration
if implemented, perhaps unless a special value is passed to
the script to keep it looping. Arguments unpassed would get
default values.)

(2023 thoughts: A new "?=" assign-if-unset for num. var., and
pre-assigning cli argument variables by name, better than
"arg" array. See <https://codeberg.org/sau/saugns/issues/5>.)
