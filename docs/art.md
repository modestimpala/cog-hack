# Art and animations

How the game draws its art, what the title screen and the new-game boot sequence do over time, and
where cog-hack adds its own part (`src/overlay/title.c`, `src/overlay/intro.c`). The timings and
colours come from UI recordings of the two screens (README "UI recordings").

## REXPaint conventions

The UI is the REX engine, so every console cell is a [REXPaint](https://www.gridsagegames.com/rexpaint/)
cell: a CP437 code, a foreground colour and a background colour (`CELL_*` in `include/cogmind.h`). A
background of 255,0,255 means "not painted": the cell is transparent and whatever is below shows
through. Layers of a drawing are separate consoles stacked in draw order.

Fonts are glyph sheets, one per cell size the current layout uses. The art font (index 4 at
3440x1440, 32x32 pixels) is a plain CP437 sheet of 16 columns, so the glyph index is the code. The
text fonts (0, the 16x32 UI font, and 2, the 32x32 map font) have their own layout: row 0 is
`' '..'?'`, row 1 `@[\]^_\`{|}~` and the shades, row 2 `A..Z`, row 3 small caps `a..z`, then frames
and tiles (`glyph_index()` in `src/overlay/font.c`). The hook draws its own text and art with these
same sheets, read in place, so it looks native at any font setting.

REXPaint's `.xp` file is gzip around a small binary: an int32 version (negative), an int32 layer
count, then per layer int32 width, int32 height and width x height cells in column-major order, each
an int32 code and six bytes of colour (fg r,g,b then bg r,g,b). The game's `data/art/*.x` files are
gzip of the same size and shape, but the ints after the version are scrambled with a key stream, so
they are not loadable as `.xp`. There is no need to: the loaded layers sit in memory, see below.

cog-hack keeps its title art as text, `art/coghack.txt`: three sections of rows (`[letters]`,
`[circuits]`, `[bright]`, the circuits' green level per cell as a hex digit), covering the whole
69x28 title canvas. It is cog-hack's own lettering, not the game's. `tools/title_art.py` turns it
into the C include the build uses, into a two-layer `.xp` for REXPaint and back, or into a PNG drawn
with the game's art font (read from one of your snapshots; nothing of the game is stored):

```sh
python3 tools/title_art.py art/coghack.txt coghack.xp      # edit in REXPaint
python3 tools/title_art.py coghack.xp art/coghack.txt      # and back
python3 tools/title_art.py art/coghack.txt coghack.png <game>/hack/snapshots/snap_N.dmp   # render
```

The render uses font 5 (the 64x64 art font at 3440x1440; pass a font index after the snapshot for
another), crops to the drawing plus a one-cell margin, and leaves everything outside the letters
transparent.

## The title screen

`XRoot > CTitle` (full screen in the art font) holds three `CTitleAnimated` consoles of 69x28 cells
at root cell 38,8 (at 3440x1440): the layers of `title.x` in draw order, traces, circuits and
letters. Each one keeps the three layers as loaded (`TITLE_LAYERS`, a vector of `ArtGrid`: width,
height, cells), with the colours baked into the art: traces grey 51,51,51, circuits dim greens from
32 to 112, letters 0,178,0. The console's own cells are the animated state. A `CText` ("Grid Sage
Games presents") and a full-screen `Console` for the opening sparkle are children of `CTitle` too.

The sequence, in seconds from the moment the title appears:

| from | to | what |
|---|---|---|
| 0 | 2.7 | the presents line and a field of sparkling dim cells |
| 2.8 | 4.0 | the traces flicker in, each cell at a random grey up to about 26 |
| 3.1 | 4.0 | the circuits sparkle in: a cell flashes about 178 green and settles to its level in 0.3 s |
| 4.1 | 4.7 | the letters pop in dark on 0,64,0 and ramp to 0,217,0; the traces flash 23,23,23 and fade to black over 0.7 s |
| 4.7 | 6 | the finished logo; then a new game begins (with a save the game loads straight into it and shows neither screen) |

The animation is data driven (particle definitions in `data/particles.bin`; the class's own virtual
functions are trivial), so cog-hack does not touch it. `title.c` watches the letters layer for its
final colour, then draws over the whole logo with the overlay: a short burst of noise where the game
painted, then our circuits sparkle in and our letters fade up the way the game's do.

The letters are 7x7 cells of double-line box drawing with one cell between them, at columns 7, 15,
23, 31, 39, 47 and 55 of the canvas. Corners cut at 45 degrees (`╔╝`, `╚╗`) round a letter, and the
joints in the outlines (`╧ ╬ ╦ ╪ ╢ ╠ ╩`) are where single-line traces attach. The circuits mix single,
double and mixed box drawing, pads (`∙`), underscores, half blocks and small ASCII bits, with a
brightness per trace.

## The boot sequence

`XRoot > CIntro` (full screen, font 2) is a scripted list of lines. Its `CText` children are the
lines, the 3x1 process cursors and 8x1 binary bursts during the scan; 1x8 `Console` children are the
rain. The game reuses the `CText` objects, so track them by position, not address.

| root cell | seconds | line |
|---|---|---|
| 2,2 | 0 | SEQUENCE INITIATED |
| 2,4 | 2.5 | REGENERATING NEURAL FIBERS, cursor until the next line |
| 2,6 | 5.5 | SCANNING NETWORK, binary bursts, then the alert VIRUS DETECTED at 7.0 |
| 2,8 | 10.0 | FORMATTING NON-VITAL INORGANICS |
| 2,10 | 11.5 | DOWNLOADING ORGANIC MEMORY, the alert FRAGMENTED at 13.8, the rain starts |
| 2,12 | 15.9 | INITIATING RECOVERY, a 36x3 bar below, the alert FAILED at 17.4 |
| 2,17 | 17.7 | SECURING SYSTEM, the okay box SECURED at 18.5 |
| 2,19 | 18.8 | EXTERNAL AUDIO ACTIVE, with a 17x7 visualizer below for ten seconds |
| 2,28 | 28.8 | ENGAGE CORE, the okay box STABLE at 30.8 |
| 2,30 | 31.3 | PREPARING CONTROL OVERLAY, cursor until the end |
| | 33.7 | every line is removed at once behind a block fade; the console goes a second later |

The styling rules:

- A line is created with its whole text and revealed one glyph per frame from the left. The newest
  glyph is 0,255,0 on 0,64,0 for a frame, then 0,178,0 on black. Spaces stay transparent, so the
  rain shows through them.
- The process cursor sits one glyph after the text: three cells lit in turn for 0.5 s each, the
  background fading from 178 to 135, then 73 to 49, then 49 to 25.
- Status boxes replace the cursor, with one space of padding and black text: alerts on 178,0,0,
  okay boxes on 0,217,0.

`intro.c` waits until the `intro_after` line is fully revealed, then types `intro_text` two rows
below it with the same reveal, cursor and box, and dissolves when the game's lines do. Any line can
be the anchor, but only the last one leaves the sequence's rows free below it.

## Recording an animation

Set `ui_record=<class>` in `hack\config.txt` and the hook records that console's children every
frame they change, from the moment it appears; `ui_record_bmp=N` adds the screen as a `.bmp` every N
frames, overlay included. `tools/re/uirec.py` lists what changed when, and `uirec.frames(path)`
gives every frame's consoles and cells for scripts.
