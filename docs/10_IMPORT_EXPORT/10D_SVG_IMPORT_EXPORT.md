# 10D — SVG Import & Export

## Overview

The SVG module enables interoperability with vector graphics tools like Inkscape, Adobe Illustrator, and Figma.

---

## SVG Export Pipeline

- Canvas elements are converted into standard W3C SVG XML node structures:
  - `StrokeElement` \(\rightarrow\) `<path d="M... C..." stroke="..." fill="none"/>`
  - `ShapeElement` \(\rightarrow\) `<rect>`, `<circle>`, `<polygon>`
  - `TextElement` \(\rightarrow\) `<text x="..." y="..." font-family="...">`
  - `Layer` \(\rightarrow\) `<g id="LayerName">`

---

## SVG Import Pipeline

- Uses **NanoSVG** to parse XML paths into internal vector paths (`SkPath`).
- Unsupported complex SVG filters or script tags are safely bypassed while importing basic geometry and styling attributes.
