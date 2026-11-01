# PrimeMarkdown Guide

## What is this

A Markdown viewer for the HP Prime G1 running Upsilon.
It renders ASCII text and LaTeX formulas from .md files.

## Install

1. Copy `primemarkdown.elf` to `c:\apps\` on the calculator
   (Upsilon USB mode: press Vars, then copy to the apps folder).
2. Put your .md documents in `c:\apps\` too.
3. Launch PrimeMarkdown from Upsilon (External app, shortcut 9).

## Supported content

- ASCII text lines (English etc.)
- Block formulas between `$$ ... $$` (can span multiple lines)
- Inline formulas between `$ ... $`

```
# This is a title

This is text with an inline formula: $x^2 + 1$

$$
\int_0^1 x^2\,dx = \frac{1}{3}
$$
```

## NOT supported

- Chinese / CJK text (DejaVu font has no CJK glyphs; such lines are skipped)
- Complex LaTeX environments may fail to render (line is skipped)

## Keys

- ENTER / DOWN / tap: next page
- UP: previous page
- ESC: back to document list
- ON: exit (Upsilon may restart - known behavior)

## Tips

- Keep formulas short (max 240 chars each, 60KB per file)
- One formula per line for best layout
- Edit a .md file and press + to rescan the file list
