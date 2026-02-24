#!/usr/bin/env python3
"""
Generate a test EPUB that exercises edge cases in spine/TOC relationships.

Edge cases covered:

  1. Multiple TOC entries → single spine item (via fragment anchors)
     - frontmatter.xhtml: Dedication (#dedication), Epigraph (#epigraph),
       Foreword (#foreword) — three TOC entries, one spine item
     - chapter3.xhtml: Chapter 3 heading + three sub-sections
       (#the-reef, #the-current, #the-depths) — four TOC entries, one spine item
     - chapter5.xhtml: Chapter 5 heading + three sub-sections
       (#isle-of-echoes, #compass-rose-atoll, #whirlpool-narrows) — four TOC entries
     - appendix.xhtml: Appendix A (#appendix-a), B (#appendix-b),
       C (#appendix-c) — three TOC entries, one spine item

  2. Single TOC entry → multiple spine items (chapter spans files)
     - Chapter 2: chapter2_part1.xhtml + chapter2_part2.xhtml — one TOC entry,
       two spine items
     - Chapter 4: chapter4_part1.xhtml + chapter4_part2.xhtml +
       chapter4_part3.xhtml — one TOC entry, three spine items

  3. Spine item with no TOC entry
     - interlude.xhtml: present in spine order, absent from TOC nav

  4. TOC entry pointing to mid-file anchor (not file start)
     - backmatter.xhtml#colophon: the file starts with an Author's Note,
       but only the mid-file Colophon anchor appears in the TOC

  5. Nested TOC hierarchy
     - Chapter 3 and Chapter 5 use nested <ol> sub-entries in the nav

  6. Normal 1:1 spine-to-TOC mapping (baseline)
     - chapter1.xhtml: one spine item, one TOC entry
"""

import io
import os
import zipfile
import uuid
from datetime import datetime

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Please install Pillow: pip install Pillow")
    exit(1)


_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_BOOKERLY_FONT = os.path.join(
    _PROJECT_ROOT, "lib", "EpdFont", "builtinFonts", "source",
    "Bookerly", "Bookerly-Regular.ttf",
)


def _get_font(size=20):
    """Get the Bookerly font at the requested size, with system fallbacks."""
    for path in [_BOOKERLY_FONT]:
        try:
            return ImageFont.truetype(path, size)
        except (OSError, IOError):
            continue
    return ImageFont.load_default(size)


def _draw_text_centered(draw, y, text, font, fill, width):
    bbox = draw.textbbox((0, 0), text, font=font)
    text_width = bbox[2] - bbox[0]
    x = (width - text_width) // 2
    draw.text((x, y), text, font=font, fill=fill)


def create_cover_image():
    """Generate a cover image and return JPEG bytes."""
    width, height = 536, 800
    bg_color = (15, 55, 65)
    text_color = (225, 220, 205)

    img = Image.new("RGB", (width, height), bg_color)
    draw = ImageDraw.Draw(img)

    font_title = _get_font(72)
    font_subtitle = _get_font(26)
    font_author = _get_font(14)
    font_ornament = _get_font(64)

    title_lines = ["Spine", "& Anchor"]
    title_y = 140
    for line in title_lines:
        _draw_text_centered(draw, title_y, line, font_title, text_color, width)
        title_y += 90

    ornament_y = title_y + 10
    _draw_text_centered(draw, ornament_y, "*", font_ornament, text_color, width)

    subtitle_y = ornament_y + 72
    _draw_text_centered(draw, subtitle_y, "A Nautical Misadventure",
                        font_subtitle, text_color, width)

    _draw_text_centered(draw, height - 70, "CROSSPOINT TEST FIXTURES",
                        font_author, text_color, width)

    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=90)
    return buf.getvalue()


# ---------------------------------------------------------------------------
#  Book metadata
# ---------------------------------------------------------------------------

BOOK_UUID = str(uuid.uuid5(uuid.NAMESPACE_URL, "crosspoint:test:spine-anchor"))
TITLE = "Spine &amp; Anchor: A Nautical Misadventure"
AUTHOR = "Crosspoint Test Fixtures"
DATE = datetime.now().strftime("%Y-%m-%d")

# ---------------------------------------------------------------------------
#  FRONTMATTER — three TOC anchors in one spine item
# ---------------------------------------------------------------------------

FRONTMATTER = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Front Matter</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h2 id="dedication">Dedication</h2>

<p>For every reader whose bookmark landed on the wrong page, and every
navigator whose chart placed the lighthouse on the wrong shore.</p>

<h2 id="epigraph">Epigraph</h2>

<blockquote>
<p>&#x201C;A captain who trusts a single table of contents has never
sailed past the edge of a spine.&#x201D;</p>
<p>&#x2014; Admiral Fragmenta, <i>On the Perils of Pagination</i></p>
</blockquote>

<h2 id="foreword">Foreword</h2>

<p>The document you hold in your hands &#x2014; or rather, the document
your e-reader is attempting to reassemble from its constituent
parts &#x2014; is deliberately, almost aggressively, tangled.</p>

<p>Some chapters sprawl across multiple files. Others cram several
table-of-contents entries into a single page. At least one section
appears in the reading order yet refuses to show up in the table of
contents at all. And if you look carefully, you will find a table of
contents entry that points not to the beginning of its file, but to a
spot squarely in the middle.</p>

<p>This is all by design. If your reader survives, it can survive
anything.</p>

</body>
</html>
"""

# ---------------------------------------------------------------------------
#  CHAPTER 1 — normal 1:1 spine-to-TOC (baseline)
# ---------------------------------------------------------------------------

CHAPTER_1 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 1</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h1>Chapter 1<br/>Setting Sail</h1>

<p>Anchora had packed three things for the voyage: a sextant with a
cracked lens, a notebook whose pages were already curling from salt air,
and an unshakeable conviction that every archipelago deserved a proper
atlas.</p>

<p>&#x201C;The trouble with charts,&#x201D; she told the harbour-master
as he untied her bowline, &#x201C;is that they assume the world sits
still long enough to be drawn.&#x201D;</p>

<p>The harbour-master, who had never left the dock, nodded politely and
threw her the rope.</p>

<p>Her sloop, the <i>Pagination</i>, nosed out of the harbour on a
following wind. The open sea stretched ahead, blue and indifferent. She
spread a fresh sheet of vellum on the chart table, uncapped her pen,
and wrote at the top in careful block letters: <b>ATLAS OF THE
UNCHARTED REACH</b>.</p>

<p>She had no idea how uncharted things were about to become.</p>

</body>
</html>
"""

# ---------------------------------------------------------------------------
#  CHAPTER 2 — one TOC entry, TWO spine items
# ---------------------------------------------------------------------------

CHAPTER_2_PART1 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 2 (Part 1)</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h1>Chapter 2<br/>The Twin Harbors</h1>

<p>The first landmark on Anchora&#x2019;s route was a pair of harbors so
close together that early cartographers had drawn them as one. Later
cartographers, attempting to correct the error, had split the entry in
two &#x2014; but neglected to update the name on the second sheet.</p>

<p>&#x201C;So which is East Harbor and which is West?&#x201D; Anchora
asked the pilot boat that came alongside.</p>

<p>&#x201C;Depends which chart you&#x2019;re reading,&#x201D; the pilot
replied cheerfully. &#x201C;On mine, you&#x2019;re entering East. On
the harbourmaster&#x2019;s, this is still West. We gave up arguing about
it years ago.&#x201D;</p>

<p>Anchora recorded the discrepancy in her notebook and sailed on
through the narrow cut that connected the two basins. The water changed
from grey-green to a deep cobalt as the bottom dropped away.</p>

</body>
</html>
"""

CHAPTER_2_PART2 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 2 (Part 2)</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<p>On the far side of the cut, the second harbor opened like a cupped
hand. Fishing boats bobbed in neat rows, their hulls painted in fading
primaries: cadmium red, cerulean blue, chrome yellow.</p>

<p>&#x201C;You&#x2019;ll want to note,&#x201D; the pilot called from
his retreating boat, &#x201C;that neither harbor appears on the
Admiralty chart. Officially, this is all open water.&#x201D;</p>

<p>Anchora drew both harbors on a single sheet &#x2014; one chart, two
basins, no ambiguity &#x2014; and labeled it with coordinates she
trusted rather than names she did not. She pinned it to the wall above
her berth and felt, briefly, that the world was a place that could be
made orderly.</p>

<p>She would not feel that way again for some time.</p>

</body>
</html>
"""

# ---------------------------------------------------------------------------
#  CHAPTER 3 — one spine item, FOUR TOC entries (chapter + 3 sub-sections)
# ---------------------------------------------------------------------------

CHAPTER_3 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 3</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h1>Chapter 3<br/>Charting the Depths</h1>

<p>Beyond the Twin Harbors, the continental shelf dropped away in three
distinct terraces. Anchora&#x2019;s fathom-line, weighted with lead and
tallow, told the story in numbers: ten, forty, two hundred.</p>

<p>Each terrace had its own character, its own light, its own hazards.
She gave each a name and &#x2014; because she was, above all, a
cartographer &#x2014; a sub-heading.</p>

<h2 id="the-reef">The Reef</h2>

<p>The first terrace was a broad shelf of coral, alive with colour and
peril in roughly equal measure. Staghorn formations reached toward the
hull like bony fingers. Fan corals swayed in the current with the slow
grace of a metronome.</p>

<p>&#x201C;Beautiful,&#x201D; Anchora murmured, leaning over the rail.
Then her keel scraped limestone, and she revised her opinion to
&#x201C;beautiful but inconvenient.&#x201D;</p>

<p>She inked the reef in hatched lines on her chart, adding a marginal
note: <i>Draft exceeding 1.5 m: proceed with caution and strong
language.</i></p>

<h2 id="the-current">The Current</h2>

<p>Below the reef shelf, the seabed fell away sharply and the water
began to move. Not the slow, companionable drift of tidal flow, but a
purposeful lateral current that pushed the <i>Pagination</i> sideways
at a rate Anchora found personally offensive.</p>

<p>She set a kedge anchor and took bearings while the boat swung in lazy
arcs. The current, she calculated, ran at two and a quarter knots on the
ebb tide, swinging to three and a half on the flood. It was the kind of
data that looked innocent in a table and lethal on a lee shore.</p>

<p>She noted it all down, adding small arrows to indicate direction and
strength. The chart was filling up nicely.</p>

<h2 id="the-depths">The Depths</h2>

<p>Past the current, the water turned from blue to black.
Anchora&#x2019;s lead-line ran out at two hundred fathoms and found
nothing. She tied on an extension. At three hundred fathoms: nothing. At
four hundred: a distant, uncertain bump.</p>

<p>&#x201C;Well,&#x201D; she said to the empty cockpit, &#x201C;there
is apparently a bottom.&#x201D;</p>

<p>She drew a single contour line on her chart and wrote <i>400+ fm</i>
beside it. Below the line she left blank space &#x2014; the
cartographer&#x2019;s admission that some things are simply too deep to
record.</p>

</body>
</html>
"""

# ---------------------------------------------------------------------------
#  INTERLUDE — in spine, NOT in TOC
# ---------------------------------------------------------------------------

INTERLUDE = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Interlude</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h1>The Phantom Island</h1>

<p>Between Chapter Three and Chapter Four, Anchora sailed past an island
that did not exist.</p>

<p>It had palm trees. It had a beach of white sand that squeaked
underfoot. It had a small but enthusiastic colony of penguins, which
was geographically improbable but zoologically undeniable.</p>

<p>She reached for her pen to add it to the atlas, then paused. The
island was not in the table of contents. It was not listed in the
index. It appeared in the reading order &#x2014; she was, after all,
reading about it right now &#x2014; but no navigation entry pointed
to it.</p>

<p>&#x201C;If a place has no entry,&#x201D; she asked one of the
penguins, &#x201C;does it really exist?&#x201D;</p>

<p>The penguin regarded her with the weary patience of a creature that
has heard this question before, then waddled back to the surf.</p>

<p>Anchora sailed on. She did not add the island to her chart. She did,
however, add penguins to her list of things that were not supposed to
be there but were.</p>

</body>
</html>
"""

# ---------------------------------------------------------------------------
#  CHAPTER 4 — one TOC entry, THREE spine items
# ---------------------------------------------------------------------------

CHAPTER_4_PART1 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 4 (Day 1)</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h1>Chapter 4<br/>The Long Voyage</h1>

<h2>Day One &#x2014; Departure</h2>

<p>The passage to the outer archipelago was, by all accounts, a
three-day sail. Anchora provisioned accordingly: three days of
hard-tack, three days of tinned sardines, and three days of the sort
of instant coffee that dissolves under protest.</p>

<p>Day one was uneventful. The wind held steady from the southeast, the
seas were moderate, and the <i>Pagination</i> made six knots on a beam
reach. Anchora spent the daylight hours sketching coastline profiles
from the deck and the evening hours transferring them to her master
chart.</p>

<p>By sunset, the Twin Harbors had sunk below the horizon and there was
nothing in any direction but water and the thin, bright line where it
met the sky.</p>

<p>She marked her position on the chart with a small cross and the
time: <i>Day 1, 18:47, all well.</i></p>

</body>
</html>
"""

CHAPTER_4_PART2 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 4 (Day 2)</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h2>Day Two &#x2014; Becalmed</h2>

<p>The wind died at dawn and did not return. The <i>Pagination</i> sat
on water so flat it reflected the clouds like a tray of mercury. The
sails hung in dispirited loops. Even the tell-tales drooped.</p>

<p>Anchora tried whistling for wind (maritime tradition), then tapping
the barometer (maritime superstition), then simply sitting in the
cockpit and staring at the horizon (maritime realism).</p>

<p>By noon the heat was ferocious. She rigged a sun-shade from the
spare jib and spent the afternoon measuring the depth with her lead-line,
which was at least productive even if it was not comfortable. The
bottom here was sixty fathoms of soft mud. She noted this on the chart,
though she suspected no one would ever care.</p>

<p>At sunset, a catspaw rippled the surface to the north. By the time
she trimmed the sails to catch it, it was gone.</p>

<p>She marked her position: <i>Day 2, 19:02, becalmed, morale stable
but coffee supply critical.</i></p>

</body>
</html>
"""

CHAPTER_4_PART3 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 4 (Day 3)</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h2>Day Three &#x2014; Arrival</h2>

<p>The wind returned overnight with the subtlety of a cannon shot. One
moment the <i>Pagination</i> was drifting; the next she was heeled over
at twenty degrees, spray flying, the rigging singing a chord in B-flat
minor.</p>

<p>Anchora, who had been asleep, arrived on deck wearing one boot and
an expression of startled competence. She reefed the main, trimmed the
jib, and pointed the bow toward the coordinates she had been given for
the outer archipelago.</p>

<p>Land appeared at noon: a low, dark smudge that resolved slowly into
individual islands, then individual trees, then individual birds
perched in the individual trees. She counted seven major islands and
an uncountable number of rocks, shoals, and ambiguous features that
might have been either.</p>

<p>She uncapped her pen. This, at last, was what she had come for.</p>

<p>She marked her position one final time: <i>Day 3, 12:15, landfall.
The atlas begins in earnest.</i></p>

</body>
</html>
"""

# ---------------------------------------------------------------------------
#  CHAPTER 5 — one spine item, nested TOC (chapter + 3 sub-entries)
# ---------------------------------------------------------------------------

CHAPTER_5 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 5</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h1>Chapter 5<br/>Archipelago of Wonders</h1>

<p>The outer archipelago comprised seven islands, but Anchora quickly
learned that three of them demanded most of her attention. The other
four were low, sandy, and profoundly uninteresting &#x2014; the kind of
islands that exist primarily to give seabirds somewhere to argue.</p>

<h2 id="isle-of-echoes">Isle of Echoes</h2>

<p>The first notable island was ringed by basalt cliffs so sheer they
formed a natural amphitheatre. Every sound bounced from wall to wall
in diminishing repetitions: the crash of a wave became a series of
claps; a shout became a conversation with oneself.</p>

<p>Anchora tested the acoustics by reading her coordinates aloud.
&#x201C;Fourteen degrees, thirty-seven minutes south,&#x201D; she
announced. The cliffs repeated it back, each echo slightly garbled,
until by the seventh repetition the island appeared to be declaring
itself at forty-seven degrees north. She noted this as a navigational
hazard.</p>

<p>The chart grew a detailed inset of the Isle of Echoes, complete with
soundings, anchorage notes, and a warning about acoustic anomalies.</p>

<h2 id="compass-rose-atoll">Compass Rose Atoll</h2>

<p>The second island was not, strictly speaking, an island at all. It
was an atoll: a ring of coral enclosing a shallow lagoon, with four
narrow passes at the cardinal points. From the air &#x2014; or from a
sufficiently tall mast &#x2014; it looked exactly like a compass rose.</p>

<p>Anchora sailed through the north pass and anchored in the lagoon.
The water was so clear she could see her anchor chain lying on the
bottom in gentle curves, like cursive script.</p>

<p>She spent a full day surveying the atoll, measuring each pass and
sounding the lagoon. The symmetry was remarkable: each pass was within
a boat-length of the same width, and the lagoon was uniformly four
fathoms deep. Nature, it seemed, had a taste for geometry.</p>

<h2 id="whirlpool-narrows">The Whirlpool Narrows</h2>

<p>Between the second and third islands, the tidal flow compressed
through a gap barely a cable&#x2019;s length wide. The result was a
whirlpool that spun with metronomic regularity: clockwise on the ebb,
counterclockwise on the flood, and in a state of churning indecision
at slack water.</p>

<p>Anchora timed the cycles, measured the diameter, and estimated the
rotational speed. She drew the whirlpool on her chart as a neat spiral
with arrows, then added the note: <i>Transit at slack water only.
Allow margin for error. Do not bring the good sextant.</i></p>

<p>She had now been in the archipelago for four days, and her chart was
beginning to look like something a real navigator might trust. This
pleased her more than she would have admitted to the penguins.</p>

</body>
</html>
"""

# ---------------------------------------------------------------------------
#  APPENDIX — one spine item, THREE TOC entries via anchors
# ---------------------------------------------------------------------------

APPENDIX = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Appendices</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h1 id="appendix-a">Appendix A<br/>Knot Types Employed</h1>

<p>The following knots were tied at various points during the voyage
and are reproduced here for the edification of the reader:</p>

<p><b>Bowline.</b> Used to secure the <i>Pagination</i> to docks,
bollards, and on one occasion a surprisingly cooperative palm tree.
Advantages: does not slip, does not jam. Disadvantages: requires two
hands, which is one more than a sailor holding a coffee cup has
available.</p>

<p><b>Cleat hitch.</b> Used at every marina. Satisfyingly quick to tie
and untie. Anchora could do it in four seconds. She timed herself.</p>

<p><b>Figure-eight.</b> Used as a stopper knot on every sheet and
halyard. The kind of knot that, once learned, the fingers tie without
consulting the brain.</p>

<h1 id="appendix-b">Appendix B<br/>Signal Flags Observed</h1>

<p>During the voyage, the following International Code of Signals flags
were observed flying from other vessels:</p>

<p><b>Alpha.</b> &#x201C;I have a diver down; keep well clear at slow
speed.&#x201D; Observed near the reef. Anchora kept well clear.</p>

<p><b>Bravo.</b> &#x201C;I am taking in, or discharging, or carrying
dangerous goods.&#x201D; Observed at the Twin Harbors. The dangerous
goods turned out to be a crate of live chickens.</p>

<p><b>Quebec.</b> &#x201C;My vessel is healthy and I request free
pratique.&#x201D; Flown by the <i>Pagination</i> herself upon arrival
at each port, mostly out of optimism.</p>

<h1 id="appendix-c">Appendix C<br/>Tidal Observations</h1>

<p>Tidal data collected during the voyage is summarized below. All
times are approximate, all heights are measured from chart datum, and
all predictions should be treated with the same confidence one extends
to a weather forecast.</p>

<p><b>Twin Harbors.</b> Semi-diurnal, range 1.8&#x2013;2.4 m. High
water approximately coincides with lunar transit. The two basins
exhibit a 12-minute phase lag, which the locals blame on a submerged
rock formation and Anchora blames on insufficient data.</p>

<p><b>Whirlpool Narrows.</b> Tidal streams reach 4.5 kn at springs.
Slack water lasts approximately 8 minutes. This is not a generous
margin.</p>

<p><b>Compass Rose Atoll.</b> Negligible tidal range inside the lagoon
(0.3 m). Currents through the passes reach 2 kn on springs but are
predictable and well-behaved. Anchora described them in her notes as
&#x201C;the only polite water in the archipelago.&#x201D;</p>

</body>
</html>
"""

# ---------------------------------------------------------------------------
#  BACKMATTER — TOC points to #colophon (mid-file), not file start
# ---------------------------------------------------------------------------

BACKMATTER = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Back Matter</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>

<h2>Author&#x2019;s Note</h2>

<p>This section has no entry in the table of contents. If you have
arrived here by paging forward from the appendices, congratulations:
you are reading in spine order, which is the only reliable way to find
content that the navigation has chosen to ignore.</p>

<p>The Author&#x2019;s Note exists to test a specific edge case: a
spine item whose table-of-contents entry points not to the beginning
of the file, but to an anchor partway through it. Everything above the
anchor is &#x201C;dark matter&#x201D; &#x2014; present in the document,
reachable by paging, but invisible to the TOC.</p>

<p>Anchora would have had opinions about this. Cartographers do not
approve of places that exist but cannot be navigated to.</p>

<h2 id="colophon">Colophon</h2>

<p>This EPUB was generated by a Python script as a test fixture for the
Crosspoint Reader project. Its spine contains fourteen items. Its table
of contents contains sixteen entries. The relationship between the two
is, by design, entertainingly non-trivial.</p>

<p>No penguins were harmed in the making of this book.</p>

</body>
</html>
"""

# ---------------------------------------------------------------------------
#  EPUB boilerplate
# ---------------------------------------------------------------------------

COVER_XHTML = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Cover</title>
<style>
body { margin: 0; padding: 0; text-align: center; }
img { max-width: 100%; max-height: 100%; }
</style>
</head>
<body>
<img src="cover.jpg" alt="Spine &amp; Anchor: A Nautical Misadventure"/>
</body>
</html>
"""

STYLESHEET = """\
body {
  font-family: serif;
  margin: 2em;
  line-height: 1.6;
}
h1 {
  font-size: 1.5em;
  text-align: center;
  margin-bottom: 1.5em;
  line-height: 1.3;
}
h2 {
  font-size: 1.15em;
  margin-top: 1.5em;
  margin-bottom: 0.5em;
}
p {
  text-indent: 1.5em;
  margin: 0.25em 0;
  text-align: justify;
}
blockquote p {
  text-indent: 0;
  margin: 0.5em 1.5em;
  font-style: italic;
}
"""

CONTAINER_XML = """\
<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

CONTENT_OPF = f"""\
<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="BookId" version="3.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="BookId">urn:uuid:{BOOK_UUID}</dc:identifier>
    <dc:title>{TITLE}</dc:title>
    <dc:creator>{AUTHOR}</dc:creator>
    <dc:language>en</dc:language>
    <dc:date>{DATE}</dc:date>
    <meta property="dcterms:modified">{DATE}T00:00:00Z</meta>
    <meta name="cover" content="cover-image"/>
  </metadata>
  <manifest>
    <item id="cover-image" href="cover.jpg" media-type="image/jpeg" properties="cover-image"/>
    <item id="cover" href="cover.xhtml" media-type="application/xhtml+xml"/>
    <item id="style" href="style.css" media-type="text/css"/>
    <item id="frontmatter" href="frontmatter.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch1" href="chapter1.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch2p1" href="chapter2_part1.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch2p2" href="chapter2_part2.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch3" href="chapter3.xhtml" media-type="application/xhtml+xml"/>
    <item id="interlude" href="interlude.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch4p1" href="chapter4_part1.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch4p2" href="chapter4_part2.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch4p3" href="chapter4_part3.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch5" href="chapter5.xhtml" media-type="application/xhtml+xml"/>
    <item id="appendix" href="appendix.xhtml" media-type="application/xhtml+xml"/>
    <item id="backmatter" href="backmatter.xhtml" media-type="application/xhtml+xml"/>
    <item id="toc" href="toc.xhtml" media-type="application/xhtml+xml" properties="nav"/>
  </manifest>
  <spine>
    <itemref idref="cover"/>
    <itemref idref="toc"/>
    <itemref idref="frontmatter"/>
    <itemref idref="ch1"/>
    <itemref idref="ch2p1"/>
    <itemref idref="ch2p2"/>
    <itemref idref="ch3"/>
    <itemref idref="interlude"/>
    <itemref idref="ch4p1"/>
    <itemref idref="ch4p2"/>
    <itemref idref="ch4p3"/>
    <itemref idref="ch5"/>
    <itemref idref="appendix"/>
    <itemref idref="backmatter"/>
  </spine>
</package>
"""

TOC_XHTML = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"
      xml:lang="en" lang="en">
<head><title>Table of Contents</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Spine &#x26; Anchor</h1>
<nav epub:type="toc">
  <ol>
    <li><a href="frontmatter.xhtml#dedication">Dedication</a></li>
    <li><a href="frontmatter.xhtml#epigraph">Epigraph</a></li>
    <li><a href="frontmatter.xhtml#foreword">Foreword</a></li>
    <li><a href="chapter1.xhtml">Chapter 1 &#x2013; Setting Sail</a></li>
    <li><a href="chapter2_part1.xhtml">Chapter 2 &#x2013; The Twin Harbors</a></li>
    <li>
      <a href="chapter3.xhtml">Chapter 3 &#x2013; Charting the Depths</a>
      <ol>
        <li><a href="chapter3.xhtml#the-reef">The Reef</a></li>
        <li><a href="chapter3.xhtml#the-current">The Current</a></li>
        <li><a href="chapter3.xhtml#the-depths">The Depths</a></li>
      </ol>
    </li>
    <li><a href="chapter4_part1.xhtml">Chapter 4 &#x2013; The Long Voyage</a></li>
    <li>
      <a href="chapter5.xhtml">Chapter 5 &#x2013; Archipelago of Wonders</a>
      <ol>
        <li><a href="chapter5.xhtml#isle-of-echoes">Isle of Echoes</a></li>
        <li><a href="chapter5.xhtml#compass-rose-atoll">Compass Rose Atoll</a></li>
        <li><a href="chapter5.xhtml#whirlpool-narrows">The Whirlpool Narrows</a></li>
      </ol>
    </li>
    <li><a href="appendix.xhtml#appendix-a">Appendix A &#x2013; Knot Types</a></li>
    <li><a href="appendix.xhtml#appendix-b">Appendix B &#x2013; Signal Flags</a></li>
    <li><a href="appendix.xhtml#appendix-c">Appendix C &#x2013; Tidal Observations</a></li>
    <li><a href="backmatter.xhtml#colophon">Colophon</a></li>
  </ol>
</nav>
</body>
</html>
"""


# ---------------------------------------------------------------------------
#  Build
# ---------------------------------------------------------------------------

def build_epub(output_path: str):
    cover_data = create_cover_image()

    with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        zf.writestr("META-INF/container.xml", CONTAINER_XML)
        zf.writestr("OEBPS/content.opf", CONTENT_OPF)
        zf.writestr("OEBPS/toc.xhtml", TOC_XHTML)
        zf.writestr("OEBPS/style.css", STYLESHEET)
        zf.writestr("OEBPS/cover.jpg", cover_data)
        zf.writestr("OEBPS/cover.xhtml", COVER_XHTML)
        zf.writestr("OEBPS/frontmatter.xhtml", FRONTMATTER)
        zf.writestr("OEBPS/chapter1.xhtml", CHAPTER_1)
        zf.writestr("OEBPS/chapter2_part1.xhtml", CHAPTER_2_PART1)
        zf.writestr("OEBPS/chapter2_part2.xhtml", CHAPTER_2_PART2)
        zf.writestr("OEBPS/chapter3.xhtml", CHAPTER_3)
        zf.writestr("OEBPS/interlude.xhtml", INTERLUDE)
        zf.writestr("OEBPS/chapter4_part1.xhtml", CHAPTER_4_PART1)
        zf.writestr("OEBPS/chapter4_part2.xhtml", CHAPTER_4_PART2)
        zf.writestr("OEBPS/chapter4_part3.xhtml", CHAPTER_4_PART3)
        zf.writestr("OEBPS/chapter5.xhtml", CHAPTER_5)
        zf.writestr("OEBPS/appendix.xhtml", APPENDIX)
        zf.writestr("OEBPS/backmatter.xhtml", BACKMATTER)
    print(f"EPUB written to {output_path}")


if __name__ == "__main__":
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(project_root, "test", "epubs", "test_spine_toc_edges.epub")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    build_epub(out)
