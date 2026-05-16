# Figures

These figures are generated with
[SVG Tiler](https://github.com/edemaine/svgtiler), using its
[chess example](https://github.com/edemaine/svgtiler/tree/main/examples/chess)
as a submodule in [svgtiler](svgtiler) subdirectory.

The local source files are the `*.asc` board diagrams, [labels.coffee](labels.coffee)
for rank/file labels, and [Maketile.args](Maketile.args) for the build
configuration. The generated `*.svg` files are committed so GitHub can render
the root README directly.

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
