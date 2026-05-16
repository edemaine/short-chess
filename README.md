# Short Chess Mate Search

Brute-force mate search for "short chess": ordinary chess pieces and movement
on an 8-file by 5-rank board. The initial position is standard chess with three
middle ranks removed, leaving one empty rank between the pawn rows.

The program answers two bounded questions:

- Can White, moving first, force checkmate in N White moves?
- After any White first move, can Black force checkmate in N Black moves?

The rules implemented are normal movement, check/checkmate, promotion, initial
two-square pawn moves, and en passant. Castling and draw rules are intentionally
omitted.

## Build and Run

```sh
make
./chess [depth|first..last] [transposition-table-mb]
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

Examples:

```sh
./chess 5 8     # search just mate-in-5 with an 8 MB transposition table
./chess 1..5 8  # search mate-in-1, mate-in-2, ..., mate-in-5
./chess 6 0     # search just mate-in-6 with the transposition table disabled
```

If omitted, the depth range defaults to `1..5` and the transposition table
defaults to `8` MB.

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
