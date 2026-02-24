#!/usr/bin/env python3
"""
Generate a small EPUB that exercises footnote detection and navigation.

Targets the footnote features from PR #1031:
  - Internal <a href> links detected as footnotes during parsing
  - Cross-file href resolution (e.g. notes.xhtml#fn3 → spine index)
  - Same-file anchor links (e.g. #fn1)
  - Footnote label normalization ([1] → 1, [*] → *, etc.)
  - Per-page footnote assignment via cumulative word counter
  - Saved-position stack for Back navigation after following a footnote
  - Nested footnote navigation (footnote that links to another footnote)

Content structure:
  - 3 prose chapters with inline footnote links pointing to notes.xhtml
  - 1 notes chapter (notes.xhtml) containing all footnote targets
  - Footnotes use a mix of numbering styles: [1], [2], *, †, [a]
  - Chapter 3 has many footnotes to test the 16-per-page cap
  - A self-referential footnote in the notes chapter tests nested navigation
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
    paths = [_BOOKERLY_FONT]
    for path in paths:
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
    bg_color = (18, 52, 35)
    text_color = (240, 225, 200)

    img = Image.new("RGB", (width, height), bg_color)
    draw = ImageDraw.Draw(img)

    font_title = _get_font(72)
    font_subtitle = _get_font(26)
    font_author = _get_font(14)
    font_ornament = _get_font(64)

    title_lines = ["Footnotes", "& Fiascos"]
    title_y = 140
    for line in title_lines:
        _draw_text_centered(draw, title_y, line, font_title, text_color, width)
        title_y += 90

    ornament_y = title_y + 10
    _draw_text_centered(draw, ornament_y, "\u2020", font_ornament, text_color, width)

    subtitle_y = ornament_y + 72
    _draw_text_centered(draw, subtitle_y, "A Thoroughly Annotated Novella",
                        font_subtitle, text_color, width)

    _draw_text_centered(draw, height - 70, "CROSSPOINT TEST FIXTURES",
                        font_author, text_color, width)

    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=90)
    return buf.getvalue()


BOOK_UUID = str(uuid.uuid4())
TITLE = "Footnotes &amp; Fiascos"
AUTHOR = "Crosspoint Test Fixtures"
DATE = datetime.now().strftime("%Y-%m-%d")

# ── XHTML content pages ──────────────────────────────────────────────

CHAPTER_1 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 1 &#x2013; The Unread Masterpiece</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Chapter 1<br/>The Unread Masterpiece</h1>

<p>It was a truth universally acknowledged that Professor Reginald
Blatherskite possessed the most heavily footnoted manuscript in the
entire history of English literature.<a href="notes.xhtml#fn1">[1]</a>
His magnum opus, <i>On the Proper Steeping of Tea</i>, ran to forty-three
pages of actual prose and one hundred and seventy-two pages of endnotes,
appendices, and cross-references to his own earlier
footnotes.<a href="notes.xhtml#fn2">[2]</a></p>

<p>&#x201C;The footnote,&#x201D; the Professor was fond of saying, &#x201C;is
the true art form. The main text is merely a scaffold upon which
to hang one&#x2019;s real thinking.&#x201D;<a href="notes.xhtml#fn3">[3]</a>
His colleagues at the University of Fothering-on-Slosh found this view
eccentric. His students found it maddening. His publisher found it
grounds for early retirement.</p>

<p>The trouble began on a Tuesday in
March,<a href="notes.xhtml#fn4">[4]</a> when Blatherskite received
a letter from the Worshipful Guild of Tea
Merchants<a href="notes.xhtml#fn-star">*</a> informing him that they
wished to publish a &#x201C;popular edition&#x201D; of his work &#x2014;
with, they delicately suggested, somewhat fewer footnotes.</p>

<p>&#x201C;Fewer footnotes!&#x201D; Blatherskite thundered, rattling his
teacup in its saucer.<a href="notes.xhtml#fn-dagger">&#x2020;</a>
&#x201C;One might as well ask a cathedral to have fewer flying
buttresses! The footnotes <i>are</i> the book! Without them the
reader would be left with nothing but a bald recitation of steeping
temperatures and leaf grades, entirely devoid of the scholarly
apparatus that transforms mere instruction into
<i>art</i>.&#x201D;<a href="notes.xhtml#fn5">[5]</a></p>

<p>His secretary, Miss Chalmers, who had typed every one of those
one hundred and seventy-two pages, said
nothing.<a href="notes.xhtml#fn6">[6]</a> She had learned long ago
that contradicting the Professor on the subject of footnotes was
as productive as debating the tides.</p>
</body>
</html>
"""

CHAPTER_2 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 2 &#x2013; The Great Footnote Rebellion</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Chapter 2<br/>The Great Footnote Rebellion</h1>

<p>The popular edition was duly commissioned. The Guild sent a young
editor named Pippa Farnsworth, who arrived at Blatherskite&#x2019;s
office carrying a red pencil and an expression of grim
determination.<a href="notes.xhtml#fn7">[7]</a></p>

<p>&#x201C;Professor,&#x201D; she began, &#x201C;I&#x2019;ve read the
manuscript. It&#x2019;s&#x2026; comprehensive.&#x201D;</p>

<p>&#x201C;Thank you,&#x201D; he beamed.</p>

<p>&#x201C;Your footnote on page four, for instance, runs to eleven
pages and includes a digression on the history of porcelain
glazing in Meissen,<a href="notes.xhtml#fn8">[8]</a> a sonnet
about Darjeeling,<a href="notes.xhtml#fn-a">[a]</a> and what appears
to be a recipe for scones.&#x201D;<a href="notes.xhtml#fn-b">[b]</a></p>

<p>&#x201C;The scone recipe is essential context,&#x201D; Blatherskite
insisted. &#x201C;One cannot discuss the proper accompaniments to a
cup of Assam<a href="notes.xhtml#fn9">[9]</a> without acknowledging
the role of the scone. And the sonnet provides emotional texture.
And the Meissen digression establishes the material conditions
under which tea was historically
consumed.&#x201D;<a href="notes.xhtml#fn10">[10]</a></p>

<p>Pippa rubbed her temples. &#x201C;Professor, the target audience for
this edition is people who just want to make a nice cup of
tea.<a href="notes.xhtml#fn11">[11]</a> They are not interested in
the material conditions of anything. They want to know: how much
leaf, how hot the water, how many
minutes.&#x201D;<a href="notes.xhtml#fn12">[12]</a></p>

<p>&#x201C;Barbarians,&#x201D; Blatherskite muttered, but he could see the
writing on the wall &#x2014; or rather, the red pencil marks all over
his manuscript. He agreed, with great reluctance, to a compromise:
the popular edition would retain footnotes, but each would be limited
to a single sentence.<a href="notes.xhtml#fn13">[13]</a></p>

<p>This, as it turned out, was like asking the Amazon to fit in a
teacup.<a href="notes.xhtml#fn-double-dagger">&#x2021;</a></p>
</body>
</html>
"""

CHAPTER_3 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter 3 &#x2013; A Page of Many Footnotes</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Chapter 3<br/>A Page of Many Footnotes</h1>

<p>The revised manuscript was a disaster. Blatherskite had technically
obeyed the one-sentence rule, but his sentences had become
monstrous<a href="notes.xhtml#fn14">[14]</a> &#x2014; sprawling,
clause-laden<a href="notes.xhtml#fn15">[15]</a> constructions
that wound<a href="notes.xhtml#fn16">[16]</a> their
way<a href="notes.xhtml#fn17">[17]</a> across entire
pages,<a href="notes.xhtml#fn18">[18]</a>
bristling<a href="notes.xhtml#fn19">[19]</a> with
semicolons,<a href="notes.xhtml#fn20">[20]</a>
em dashes,<a href="notes.xhtml#fn21">[21]</a> and parenthetical
asides<a href="notes.xhtml#fn22">[22]</a> that were themselves
footnotes<a href="notes.xhtml#fn23">[23]</a> in all but
name.<a href="notes.xhtml#fn24">[24]</a></p>

<p>Pippa stared at the page. &#x201C;Footnote fourteen alone is
three hundred words
long.&#x201D;<a href="notes.xhtml#fn25">[25]</a></p>

<p>&#x201C;But it is one sentence,&#x201D; Blatherskite said
triumphantly.<a href="notes.xhtml#fn26">[26]</a></p>

<p>&#x201C;It contains seventeen
semicolons.&#x201D;<a href="notes.xhtml#fn27">[27]</a></p>

<p>&#x201C;The semicolon is a perfectly legitimate piece of
punctuation.&#x201D;<a href="notes.xhtml#fn28">[28]</a></p>

<p>&#x201C;And four sets of nested
parentheses.&#x201D;<a href="notes.xhtml#fn29">[29]</a></p>

<p>&#x201C;Parentheses are the footnote&#x2019;s humbler
cousin.&#x201D;<a href="notes.xhtml#fn-section">&#xA7;</a></p>

<p>At this point Pippa did something no editor had ever dared to
do in the Professor&#x2019;s presence: she took out her red pencil
and, with a single bold stroke, crossed out footnote
fourteen entirely.<a href="notes.xhtml#fn30">[30]</a></p>

<p>The silence that followed could have curdled
milk.<a href="notes.xhtml#fn31">[31]</a></p>
</body>
</html>
"""

NOTES_CHAPTER = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Notes</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Notes</h1>

<p id="fn1"><b>1.</b> This claim has been disputed by Dr. Meredith
Plonk of the University of Nether Wallop, whose own manuscript on
the history of the semi-colon contains, by her count, more footnotes per
page. The dispute remains unresolved and is the subject of a further
footnote in Blatherskite&#x2019;s unpublished appendix.</p>

<p id="fn2"><b>2.</b> The appendices included, among other things, a
fold-out genealogy of the Camellia sinensis plant, a hand-drawn map of
every tea estate in Assam, and a pocket containing an actual tea bag,
which the publisher refused to include on grounds of cost and
hygiene.</p>

<p id="fn3"><b>3.</b> This quotation first appeared in Blatherskite&#x2019;s
keynote address to the 1987 Symposium on Scholarly Marginalia, itself
delivered entirely from the footnotes of his prepared
remarks.<a href="notes.xhtml#fn-meta">[meta]</a></p>

<p id="fn4"><b>4.</b> A Tuesday, Blatherskite later noted, being the
optimal day for receiving bad news, as it allows the full working week
to process one&#x2019;s outrage before the restorative effects of the
weekend.</p>

<p id="fn-star"><b>*</b> The Worshipful Guild of Tea Merchants, est.
1743, whose motto &#x2014; &#x201C;Infuse, Imbibe, Invoice&#x201D; &#x2014;
Blatherskite considered the pinnacle of English brevity.</p>

<p id="fn-dagger"><b>&#x2020;</b> The teacup, a Wedgwood Jasperware
piece circa 1812, survived this incident but would not survive the
events of Chapter 3.</p>

<p id="fn5"><b>5.</b> It is worth noting that Blatherskite&#x2019;s
definition of &#x201C;art&#x201D; was unusually broad, encompassing as
it did railway timetables, tax returns, and the instructions on the
back of a shampoo bottle, all of which he had at various points
annotated with footnotes.</p>

<p id="fn6"><b>6.</b> Miss Chalmers&#x2019;s silence was, in its own
way, more eloquent than any of the Professor&#x2019;s footnotes. She had
developed a system of sighs graded from one (mild exasperation) to
seven (existential despair). This occasion warranted a four.</p>

<p id="fn7"><b>7.</b> The red pencil was a Staedtler Tradition 110,
chosen for its ability to make marks that could be seen from across the
room. Pippa had been through three of them on the train up from
London.</p>

<p id="fn8"><b>8.</b> The Meissen digression, while fascinating, had no
discernible connection to tea, porcelain teapots, or indeed any vessel
from which one might consume a beverage. It concerned, primarily, the
love life of Augustus the Strong.</p>

<p id="fn-a"><b>a.</b> The sonnet, titled &#x201C;Ode to a Leaf
Unfurling in Hot Water,&#x201D; was written in Petrarchan form and
scanned adequately, though the rhyming of &#x201C;Darjeeling&#x201D; with
&#x201C;feeling&#x201D; was considered a stretch by the three people who
had read it.</p>

<p id="fn-b"><b>b.</b> The scone recipe, to be fair, was excellent. Miss
Chalmers made them every Friday.</p>

<p id="fn9"><b>9.</b> Assam, a tea-growing region in northeastern India,
produces a malty, full-bodied black tea that Blatherskite considered
&#x201C;the backbone of any proper English breakfast,&#x201D; a view he had
defended in no fewer than twelve published footnotes across six different
works.</p>

<p id="fn10"><b>10.</b> The material conditions argument was
Blatherskite&#x2019;s favorite rhetorical device. He had once used it to
justify a footnote about the price of coal in Victorian Sheffield in a
paper about jasmine tea.</p>

<p id="fn11"><b>11.</b> &#x201C;A nice cup of tea&#x201D; being, in
Blatherskite&#x2019;s view, an oxymoron. Tea was not &#x201C;nice.&#x201D;
Tea was &#x201C;profound.&#x201D;</p>

<p id="fn12"><b>12.</b> Three grams, ninety-six degrees Celsius, four
minutes. But to say only that, according to Blatherskite, was like
describing the Mona Lisa as &#x201C;a painting of a woman.&#x201D;</p>

<p id="fn13"><b>13.</b> A compromise Blatherskite compared, in a letter
to the <i>Times Literary Supplement</i>, to asking Michelangelo to
paint the Sistine Chapel ceiling in emulsion.</p>

<p id="fn-double-dagger"><b>&#x2021;</b> This metaphor is, of course,
itself a candidate for a footnote. Blatherskite would have noted that
the Amazon discharges roughly 209,000 cubic meters of water per second,
whereas a standard teacup holds approximately 0.00024 cubic meters,
making the ratio approximately 870,833,333 to one.</p>

<p id="fn14"><b>14.</b> &#x201C;Monstrous&#x201D; in the etymological
sense &#x2014; from the Latin <i>monstrum</i>, a portent or warning &#x2014;
which is exactly what these sentences were.</p>

<p id="fn15"><b>15.</b> Clause-laden in the manner of a Victorian
Christmas tree: ornate, precarious, and likely to topple if anyone
breathed on it.</p>

<p id="fn16"><b>16.</b> &#x201C;Wound&#x201D; as in followed a winding
path, not as in inflicted injury, though readers of the manuscript
might argue otherwise.</p>

<p id="fn17"><b>17.</b> The use of &#x201C;their way&#x201D; here is
deliberate; by this point the sentences had achieved a kind of
autonomous agency and were going where they pleased.</p>

<p id="fn18"><b>18.</b> Including, in one memorable instance, a sentence
that began on page seven and did not conclude until page twelve.</p>

<p id="fn19"><b>19.</b> Like a hedgehog in a bad mood.</p>

<p id="fn20"><b>20.</b> Blatherskite considered the semicolon the
noblest of punctuation marks; a full stop for people with commitment
issues.</p>

<p id="fn21"><b>21.</b> The em dash &#x2014; that most dramatic of
horizontal lines &#x2014; was Blatherskite&#x2019;s second-favorite
punctuation mark after the footnote indicator itself.</p>

<p id="fn22"><b>22.</b> Parenthetical asides (like this one) were, in
Blatherskite&#x2019;s typology, &#x201C;footnotes that haven&#x2019;t
yet realized their full potential.&#x201D;</p>

<p id="fn23"><b>23.</b> A footnote about footnotes. We have reached
peak recursion.</p>

<p id="fn24"><b>24.</b> &#x201C;In all but name&#x201D; &#x2014; the
saddest phrase in the English language, according to Blatherskite, who
had written a footnote about it.</p>

<p id="fn25"><b>25.</b> Three hundred and twelve, to be precise. Pippa
counted twice.</p>

<p id="fn26"><b>26.</b> Technically correct &#x2014; the best kind of
correct, and the only kind Blatherskite recognized.</p>

<p id="fn27"><b>27.</b> Eighteen, actually. Pippa had missed one lurking
between a parenthetical remark and a subordinate clause on what she
described as &#x201C;the dark side of the page.&#x201D;</p>

<p id="fn28"><b>28.</b> Blatherskite had once written an entire paper
defending the semicolon against what he called &#x201C;the barbarous
ascendancy of the full stop.&#x201D; It contained no full stops. It was
one sentence long. It ran to fourteen pages.</p>

<p id="fn29"><b>29.</b> The deepest nesting &#x2014; four levels of
parentheses &#x2014; occurred in a passage about the water temperature
preferred by the third Earl of Grey (the man, not the tea (though also
the tea (which was named after the man (who preferred his water at
exactly ninety-four degrees)))).</p>

<p id="fn-section"><b>&#xA7;</b> A claim that parentheses everywhere
found deeply offensive.</p>

<p id="fn30"><b>30.</b> The crossing-out was performed with a single
horizontal stroke of the Staedtler Tradition 110 (see note 7), applied
with the confident pressure of an editor who has been pushed beyond the
limits of scholarly patience.</p>

<p id="fn31"><b>31.</b> It did, in fact, curdle the milk. The Professor
had left a small jug of semi-skimmed on his desk, and by the time he
spoke again it had turned.</p>

<p id="fn-meta"><b>meta.</b> You have followed a footnote from within a
footnote. The Professor would be proud. This is the deepest level of
annotation in the book. There is nothing further down. You may now press
Back to return whence you came &#x2014; or keep pressing Back to unwind
the entire chain of scholarly digression, like Theseus following
Ariadne&#x2019;s thread out of the footnote
labyrinth.<a href="notes.xhtml#fn-star">*</a></p>
</body>
</html>
"""

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
<img src="cover.jpg" alt="Footnotes &amp; Fiascos"/>
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
p {
  text-indent: 1.5em;
  margin: 0.25em 0;
  text-align: justify;
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
    <item id="ch1" href="chapter1.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch2" href="chapter2.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch3" href="chapter3.xhtml" media-type="application/xhtml+xml"/>
    <item id="notes" href="notes.xhtml" media-type="application/xhtml+xml"/>
    <item id="toc" href="toc.xhtml" media-type="application/xhtml+xml" properties="nav"/>
  </manifest>
  <spine>
    <itemref idref="cover"/>
    <itemref idref="toc"/>
    <itemref idref="ch1"/>
    <itemref idref="ch2"/>
    <itemref idref="ch3"/>
    <itemref idref="notes"/>
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
<h1>Footnotes &amp; Fiascos</h1>
<nav epub:type="toc">
  <ol>
    <li><a href="chapter1.xhtml">Chapter 1 &#x2013; The Unread Masterpiece</a></li>
    <li><a href="chapter2.xhtml">Chapter 2 &#x2013; The Great Footnote Rebellion</a></li>
    <li><a href="chapter3.xhtml">Chapter 3 &#x2013; A Page of Many Footnotes</a></li>
    <li><a href="notes.xhtml">Notes</a></li>
  </ol>
</nav>
</body>
</html>
"""


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
        zf.writestr("OEBPS/chapter1.xhtml", CHAPTER_1)
        zf.writestr("OEBPS/chapter2.xhtml", CHAPTER_2)
        zf.writestr("OEBPS/chapter3.xhtml", CHAPTER_3)
        zf.writestr("OEBPS/notes.xhtml", NOTES_CHAPTER)
    print(f"EPUB written to {output_path}")


if __name__ == "__main__":
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(project_root, "test", "epubs", "test_footnotes.epub")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    build_epub(out)
