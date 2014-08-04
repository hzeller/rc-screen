Remote controlled projector screen
==================================

Function
--------
Remote control for projectro screen on an Attiny 24.
Reacts on remote control signal from Epson EMP-TW600 projector.

   - Remote control ON button brings screen down (so when the projector is switched on, screen comes down)
   - OFF button brings screen up (projector off, screen up)

Also, the ON button in fact toggles the screen - that way it is possible to
move up the screen while the projector stays on (in my case, the screen covers a sliding door, so that
is convenient :) )

Inputs
------

   - IR-receiver (some common TSOP38 or similar)
   - rotation detector (cny77) (via Schmitt-Trigger input)
   - end-switch

Outputs
-------

   - motor driver in h-bridge configuration (754410 or similar)
   - bias-output for cny77 Schmitt-Trigger input.
   - status LED

The encoder wheel is written in PostScript
https://github.com/hzeller/postscript-hacks/blob/master/encoder-wheel.ps

Here a (bad quality recorded) example:
https://www.youtube.com/watch?v=lvHZYmplDkQ

There is no PCB or something, just directly raster-boarded; the screen is
a IKEA Tupplur blind.

![Screen Assembly][assembly]

[assembly]: https://github.com/hzeller/rc-screen/raw/master/img/assembly.jpg
