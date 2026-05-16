# Figures

These figures are generated with
[SVG Tiler](https://github.com/edemaine/svgtiler), using its
[chess example](https://github.com/edemaine/svgtiler/tree/main/examples/chess)
as a submodule in [svgtiler](svgtiler) subdirectory.

The local source files are the `*.ssv` board diagrams, [move.coffee](move.coffee)
for last-move annotations, [labels.coffee](labels.coffee) for rank/file labels,
and [Maketile.args](Maketile.args) for the build configuration. The generated
`*.svg` files are committed so GitHub can render the root README directly.

In non-initial positions, a source square marked `<` and a moved piece marked
with `>` draw the purple last-move line. For example, `N>` means the knight
ended on that square, and `<` marks where it came from.

## Setup

Make sure you have the `svgtiler` submodule:

```sh
git submodule update --init --recursive
```

Or you can initially clone with submodules enabled:

```sh
git clone --recurse-submodules git@github.com:edemaine/short-chess.git
```

Install SVG Tiler:

```sh
npm install -g svgtiler
```

## Building

Rebuild from the repository root:

```sh
make figures
```

Or rebuild from this directory:

```sh
svgtiler
```
