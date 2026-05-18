# Short Chess Mate Search

Brute-force mate search for ***short chess***: ordinary chess pieces and movement
on an 8-file board with 4 or more ranks.
The initial position is standard chess with middle ranks removed.
On a 4×8 board, this means every square is occupied by a piece.
On a 5×8 board, this leaves one empty rank between the pawn rows.
This chess variant was introduced by Joseph Palin as a way to approach 8×8 chess.

The program answers the following questions for a number *n*:

- Can White, moving first, force checkmate in *n* White moves?
- After any White first move, can Black force checkmate in *n* Black moves?
- Instead of checkmate (which requires reaching the game's end), what if
  the goal is just to reach a specified material advantage after *n* moves?

The rules implemented are normal movement, check/checkmate, promotion, initial
two-square pawn moves, and en passant. Castling and draw rules are intentionally
omitted.

[Figures](figures) made with [SVG Tiler](https://github.com/edemaine/svgtiler).

## 4×8 Results

The 4×8 starting board is:

![4x8 starting board](figures/4x8-start.svg)

### Mate-in-1

White has two immediate winning moves (mate-in-1). Can you find them?

<details>
<summary>Show White's mate-in-1 moves</summary>

1\. White e2f3#

![4x8 e2f3 mate](figures/4x8-mate-e2f3.svg)

1\. White g2f3#

![4x8 g2f3 mate](figures/4x8-mate-g2f3.svg)

</details>

### Mate-in-3

If you forbid those two trivial winning first moves
(as suggested by Martin Demaine and implemented in the code by `FORBID_TRIVIAL_4X8_WIN=1`),
White has a unique winning first move for mate-in-3. Can you find it?

<details>
<summary>Show White's mate-in-3 strategy</summary>

1\. White b1c3

![4x8 mate-in-3 after b1c3](figures/4x8-mate3-1-b1c3.svg)

1\... Black d4c3 (only legal reply)

![4x8 mate-in-3 after d4c3](figures/4x8-mate3-1-black-d4c3.svg)

2\. White e2f3

![4x8 mate-in-3 after e2f3](figures/4x8-mate3-2-e2f3.svg)

2\... Black e4d4 (only legal reply)

![4x8 mate-in-3 after e4d4](figures/4x8-mate3-2-black-e4d4.svg)

3\. White b2c3#

![4x8 mate-in-3 after b2c3 mate](figures/4x8-mate3-3-b2c3.svg)

</details>

## 5×8 Results

The 5×8 starting board is:

![5x8 starting board](figures/5x8-start.svg)

So far, this exhaustive search has confirmed that 5×8 chess has no winning
strategy for White or Black up to mate-in-11
(11 White moves and 10 or 11 Black moves, for a total of 20 or 21 plies).

More interesting, White can force a material advantage of 500 (one Rook
in [Shannon's point values](https://www.chessprogramming.org/Point_Value))
after the first 7 White moves.

## Building

```sh
make
```

The number of ranks is a compile-time option from 4 through 8:

```sh
make OPTIONS=-DBOARD_RANKS=4  # 4x8
make OPTIONS=-DBOARD_RANKS=5  # default 5x8
make OPTIONS=-DBOARD_RANKS=8  # ordinary 8x8 board, without castling
```

For 4×8, the two immediate winning first moves can be excluded from White's
initial move choice:

```sh
make OPTIONS="-DBOARD_RANKS=4 -DFORBID_TRIVIAL_4X8_WIN=1"
```

By default, transposition-table entries store two independent 64-bit
[Zobrist hashing](https://en.wikipedia.org/wiki/Zobrist_hashing) keys for
collision verification. To build the smaller one-key variant:

```sh
make OPTIONS=-DTT_DOUBLE_HASH=0
```

The one-key variant is less safe because a rare hash collision could confuse
two different positions, but it fits more transposition-table entries in the
same memory budget.

## Usage

```sh
./chess [options]
```

Examples:

```sh
./chess -d 5 -t 8  # search just depth 5 with an 8 MB transposition table
./chess --depth 1..5 --tt 8  # search depths 1, 2, 3, 4, and 5
./chess --depth 6 --tt 0  # search depth 6 with the transposition table disabled
./chess --depth 3 --goal material:300 --weights shannon  # force +3 pawns
./chess --depth 3 --goal material --weights shannon  # maximize guaranteed material
```

Options:

```text
-d, --depth N|A..B       own-move depth range, within 1..255 (default 1..5)
-t, --tt MB              transposition table size in MB (default 8; 0 disables)
-g, --goal GOAL          mate, material, or material:K (default mate)
-w, --weights NAME       shannon, turing, coxeter, or kaufman (default shannon)
-h, --help               show usage
```

The `mate` goal asks whether the side to move can force checkmate within the
specified number of its own moves. The `material` goal computes the maximum
material advantage the side to move can force at the search horizon, with
checkmate reported as `+infinity` and getting checkmated as `-infinity`. The
`material:K` goal asks whether the side to move can force a material advantage
of at least `K` at the search horizon, no matter how the opponent replies.
Checkmate still satisfies `material:K` early. Material values are measured in
the selected centipawn weight scale, so under the default Shannon weights
`material:300` means a three-pawn advantage or equivalent.

The material weights are from the
[Chessprogramming wiki point-value table](https://www.chessprogramming.org/Point_Value).
The default `shannon` set is Claude Shannon's `{100, 300, 300, 500, 900}` for
pawn, knight, bishop, rook, and queen. The other built-in sets are `turing`
`{100, 300, 350, 500, 1000}`, `coxeter`
`{100, 300, 350, 550, 1000}`, and `kaufman`
`{100, 350, 350, 525, 1000}`.

## Main Optimizations

- Move ordering prioritizes checking moves, promotions, and high-value captures
  so failed branches are usually refuted earlier.
- A fixed-size transposition table caches exact search results for
  `(position, remaining moves)`. It uses
  [Zobrist hashing](https://en.wikipedia.org/wiki/Zobrist_hashing) and a
  configurable memory budget instead of trying to store every reachable
  position.
- Position state is updated incrementally during move making, including king
  locations, Zobrist keys, and color/piece bitboards.
- Move generation uses the bitboards to iterate directly over the side-to-move's
  pieces by type, while retaining a square array for simple destination lookups.
- Precomputed bit masks describe knight moves, king moves, pawn attacks, and
  sliding-piece rays from each square, reducing coordinate-loop work in move
  generation and check detection.
- Move generation uses fixed-capacity move lists to keep the recursive search
  allocation-free on its hot path.
