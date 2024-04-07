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
