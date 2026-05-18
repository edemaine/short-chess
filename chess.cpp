#include <bits/stdc++.h>
using namespace std;

// Board height. Valid values are 4..8; default is 5x8 short chess.
#ifndef BOARD_RANKS
#define BOARD_RANKS 5
#endif

static constexpr int H = BOARD_RANKS;
static constexpr int W = 8;
static constexpr int N_SQUARES = H * W;
static_assert(H >= 4 && H <= 8, "BOARD_RANKS must be in 4..8");
static constexpr uint64_t BOARD_MASK =
    N_SQUARES == 64 ? ~0ULL : ((1ULL << N_SQUARES) - 1);

// Whether to store two independent Zobrist keys per transposition-table entry.
// Set to 0 to fit more entries with weaker collision protection.
#ifndef TT_DOUBLE_HASH
#define TT_DOUBLE_HASH 1
#endif

// Whether to omit White's two immediate 4x8 winning first moves from root
// search and root move listings, exposing the next shortest strategy.
#ifndef FORBID_TRIVIAL_4X8_WIN
#define FORBID_TRIVIAL_4X8_WIN 0
#endif

static_assert(!FORBID_TRIVIAL_4X8_WIN || H == 4,
              "FORBID_TRIVIAL_4X8_WIN requires BOARD_RANKS=4");

enum Color { WHITE = 0, BLACK = 1 };
enum PieceType { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING };

#if FORBID_TRIVIAL_4X8_WIN
bool forbidTrivial4x8WinForNextMoveGen = false;
#endif

// A move is encoded by source and destination square indices plus optional
// promotion piece, stored as lowercase q/r/b/n independent of side.
struct Move {
    uint8_t from = 0;
    uint8_t to = 0;
    char promo = 0; // 0, 'q', 'r', 'b', 'n'

    Move() = default;
    Move(int from, int to, char promo = 0)
        : from(static_cast<uint8_t>(from)),
          to(static_cast<uint8_t>(to)),
          promo(promo) {}
};

static constexpr int MAX_MOVES = 256;

struct MoveList {
    array<Move, MAX_MOVES> moves{};
    int n = 0;

    void clear() { n = 0; }
    bool empty() const { return n == 0; }

    void push_back(const Move& m) {
        assert(n < MAX_MOVES);
        moves[n++] = m;
    }

    Move* begin() { return moves.data(); }
    Move* end() { return moves.data() + n; }
    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + n; }
};

// Board squares contain piece letters, using uppercase for White, lowercase
// for Black, and '.' for empty. ep stores the current en-passant target square.
struct Position {
    array<char, H * W> b{};
    array<uint64_t, 2> colorBB{};
    array<uint64_t, 6> pieceBB{};
    array<int, 2> king = {-1, -1};
    uint64_t key = 0;
#if TT_DOUBLE_HASH
    uint64_t key2 = 0;
#endif
    Color side = WHITE;
    int ep = -1; // En-passant target square, or -1.
};

// Convert between row/column coordinates and the flat board index.
int idx(int r, int c) { return r * W + c; }
int row(int s) { return s / W; }
int col(int s) { return s % W; }

// True when a row/column coordinate is on the board.
bool inb(int r, int c) {
    return r >= 0 && r < H && c >= 0 && c < W;
}

// Piece and color classification helpers.
bool isWhite(char p) { return p >= 'A' && p <= 'Z'; }
bool isBlack(char p) { return p >= 'a' && p <= 'z'; }
bool isEmpty(char p) { return p == '.'; }
bool isKing(char p) { return p == 'K' || p == 'k'; }
char lowerPiece(char p) { return isWhite(p) ? char(p - 'A' + 'a') : p; }

struct PieceWeights {
    string name;
    int pawn;
    int knight;
    int bishop;
    int rook;
    int queen;
};

PieceWeights shannonWeights() {
    return {"shannon", 100, 300, 300, 500, 900};
}

PieceWeights turingWeights() {
    return {"turing", 100, 300, 350, 500, 1000};
}

PieceWeights coxeterWeights() {
    return {"coxeter", 100, 300, 350, 550, 1000};
}

PieceWeights kaufmanWeights() {
    return {"kaufman", 100, 350, 350, 525, 1000};
}

PieceWeights currentWeights = shannonWeights();

bool parseWeights(const string& name, PieceWeights& out) {
    if (name == "shannon") {
        out = shannonWeights();
    } else if (name == "turing") {
        out = turingWeights();
    } else if (name == "coxeter") {
        out = coxeterWeights();
    } else if (name == "kaufman") {
        out = kaufmanWeights();
    } else {
        return false;
    }
    return true;
}

string weightsDescription(const PieceWeights& w) {
    ostringstream out;
    out << w.name << " weights: "
        << "P=" << w.pawn
        << ", N=" << w.knight
        << ", B=" << w.bishop
        << ", R=" << w.rook
        << ", Q=" << w.queen;
    return out.str();
}

int pieceValue(char p) {
    switch (lowerPiece(p)) {
        case 'q': return currentWeights.queen;
        case 'r': return currentWeights.rook;
        case 'b': return currentWeights.bishop;
        case 'n': return currentWeights.knight;
        case 'p': return currentWeights.pawn;
        default: return 0;
    }
}

// Return the opposite color.
Color other(Color c) {
    return c == WHITE ? BLACK : WHITE;
}

// True when p is a non-empty piece belonging to side.
bool sameColor(char p, Color side) {
    if (isEmpty(p)) return false;
    return side == WHITE ? isWhite(p) : isBlack(p);
}

// True when p is a non-empty enemy piece from side's perspective.
bool oppColor(char p, Color side) {
    if (isEmpty(p)) return false;
    return side == WHITE ? isBlack(p) : isWhite(p);
}

// Make a side-colored piece from a lowercase piece letter.
char makePiece(Color side, char lower) {
    return side == WHITE ? char(lower - 'a' + 'A') : lower;
}

int pieceType(char p) {
    switch (lowerPiece(p)) {
        case 'p': return 0;
        case 'n': return 1;
        case 'b': return 2;
        case 'r': return 3;
        case 'q': return 4;
        case 'k': return 5;
        default: return -1;
    }
}

int pieceIndex(char p) {
    int type = pieceType(p);
    if (type < 0) return -1;
    return (isWhite(p) ? 0 : 6) + type;
}

uint64_t squareBit(int s) {
    return 1ULL << s;
}

int popSquare(uint64_t& bb) {
    int s = __builtin_ctzll(bb);
    bb &= bb - 1;
    return s;
}

int firstSquare(uint64_t bb) {
    return __builtin_ctzll(bb);
}

int lastSquare(uint64_t bb) {
    return 63 - __builtin_clzll(bb);
}

void addToBitboards(Position& p, int s, char pc) {
    int type = pieceType(pc);
    if (type < 0) return;

    uint64_t b = squareBit(s);
    p.colorBB[isWhite(pc) ? WHITE : BLACK] |= b;
    p.pieceBB[type] |= b;
}

void removeFromBitboards(Position& p, int s, char pc) {
    int type = pieceType(pc);
    if (type < 0) return;

    uint64_t b = squareBit(s);
    p.colorBB[isWhite(pc) ? WHITE : BLACK] &= ~b;
    p.pieceBB[type] &= ~b;
}

void computeBitboards(Position& p) {
    p.colorBB = {};
    p.pieceBB = {};

    for (int s = 0; s < H * W; s++) {
        addToBitboards(p, s, p.b[s]);
    }
}

struct Geometry {
    array<uint64_t, N_SQUARES> knight{};
    array<uint64_t, N_SQUARES> king{};
    array<array<uint64_t, N_SQUARES>, 2> pawnAttack{};
    array<array<uint64_t, N_SQUARES>, 2> pawnAttacker{};
    array<array<uint64_t, N_SQUARES>, 8> ray{};
    array<bool, 8> rayIncreasing{};

    Geometry() {
        static const int knightD[8][2] = {
            {1,2},{2,1},{2,-1},{1,-2},
            {-1,-2},{-2,-1},{-2,1},{-1,2}
        };
        static const int rayD[8][2] = {
            {1, 0}, {-1, 0}, {0, 1}, {0, -1},
            {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
        };

        for (int s = 0; s < N_SQUARES; s++) {
            int r = row(s), c = col(s);

            for (auto& d : knightD) {
                int rr = r + d[0], cc = c + d[1];
                if (inb(rr, cc)) knight[s] |= squareBit(idx(rr, cc));
            }

            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    if (dr == 0 && dc == 0) continue;
                    int rr = r + dr, cc = c + dc;
                    if (inb(rr, cc)) king[s] |= squareBit(idx(rr, cc));
                }
            }

            for (Color side : {WHITE, BLACK}) {
                int dir = side == WHITE ? -1 : 1;
                for (int dc : {-1, 1}) {
                    int rr = r + dir, cc = c + dc;
                    if (inb(rr, cc)) {
                        int to = idx(rr, cc);
                        pawnAttack[side][s] |= squareBit(to);
                        pawnAttacker[side][to] |= squareBit(s);
                    }
                }
            }

            for (int dir = 0; dir < 8; dir++) {
                int dr = rayD[dir][0], dc = rayD[dir][1];
                rayIncreasing[dir] = dr * W + dc > 0;

                int rr = r + dr, cc = c + dc;
                while (inb(rr, cc)) {
                    ray[dir][s] |= squareBit(idx(rr, cc));
                    rr += dr;
                    cc += dc;
                }
            }
        }
    }
};

const Geometry& geom() {
    static const Geometry g;
    return g;
}

uint64_t splitmix64(uint64_t& x) {
    x += 0x9e3779b97f4a7c15ULL;
    uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

struct Zobrist {
    array<array<uint64_t, H * W>, 12> piece{};
#if TT_DOUBLE_HASH
    array<array<uint64_t, H * W>, 12> piece2{};
#endif
    array<uint64_t, H * W> ep{};
#if TT_DOUBLE_HASH
    array<uint64_t, H * W> ep2{};
#endif
    uint64_t blackToMove = 0;
#if TT_DOUBLE_HASH
    uint64_t blackToMove2 = 0;
#endif

    Zobrist() {
        uint64_t seed = 0x0c0ffee123456789ULL;
        for (int p = 0; p < 12; p++) {
            for (int s = 0; s < H * W; s++) {
                piece[p][s] = splitmix64(seed);
#if TT_DOUBLE_HASH
                piece2[p][s] = splitmix64(seed);
#endif
            }
        }
        for (int s = 0; s < H * W; s++) {
            ep[s] = splitmix64(seed);
#if TT_DOUBLE_HASH
            ep2[s] = splitmix64(seed);
#endif
        }
        blackToMove = splitmix64(seed);
#if TT_DOUBLE_HASH
        blackToMove2 = splitmix64(seed);
#endif
    }
};

const Zobrist& zobrist() {
    static const Zobrist z;
    return z;
}

void computeKeys(Position& p) {
    const Zobrist& z = zobrist();
    p.key = 0;
#if TT_DOUBLE_HASH
    p.key2 = 0;
#endif

    for (int s = 0; s < H * W; s++) {
        int pi = pieceIndex(p.b[s]);
        if (pi >= 0) {
            p.key ^= z.piece[pi][s];
#if TT_DOUBLE_HASH
            p.key2 ^= z.piece2[pi][s];
#endif
        }
    }

    if (p.side == BLACK) {
        p.key ^= z.blackToMove;
#if TT_DOUBLE_HASH
        p.key2 ^= z.blackToMove2;
#endif
    }
    if (p.ep >= 0) {
        p.key ^= z.ep[p.ep];
#if TT_DOUBLE_HASH
        p.key2 ^= z.ep2[p.ep];
#endif
    }
}

// Build the starting position for an H-rank, 8-file board. This is ordinary
// chess with the middle ranks removed when H < 8.
Position initialPosition() {
    Position p;
    p.b.fill('.');

    string blackBack = "rnbqkbnr";
    string blackPawns = "pppppppp";
    string whitePawns = "PPPPPPPP";
    string whiteBack = "RNBQKBNR";

    for (int c = 0; c < W; c++) {
        p.b[idx(0, c)] = blackBack[c];
        p.b[idx(1, c)] = blackPawns[c];
        p.b[idx(H - 2, c)] = whitePawns[c];
        p.b[idx(H - 1, c)] = whiteBack[c];
    }

    p.side = WHITE;
    p.king[WHITE] = idx(H - 1, 4);
    p.king[BLACK] = idx(0, 4);
    computeBitboards(p);
    computeKeys(p);
    return p;
}

// Return side's king square, or -1 if no king is present.
int findKing(const Position& p, Color side) {
    char k = side == WHITE ? 'K' : 'k';
    for (int i = 0; i < H * W; i++) {
        if (p.b[i] == k) return i;
    }
    return -1;
}

// True when sq is attacked by any piece of attacker.
bool squareAttackedBy(const Position& p, int sq, Color attacker) {
    const Geometry& g = geom();
    uint64_t attackers = p.colorBB[attacker];

    if (g.pawnAttacker[attacker][sq] & attackers & p.pieceBB[PAWN]) return true;
    if (g.knight[sq] & attackers & p.pieceBB[KNIGHT]) return true;
    if (g.king[sq] & attackers & p.pieceBB[KING]) return true;

    uint64_t occupied = p.colorBB[WHITE] | p.colorBB[BLACK];

    for (int dir = 0; dir < 8; dir++) {
        uint64_t blockers = g.ray[dir][sq] & occupied;
        if (!blockers) continue;

        int s = g.rayIncreasing[dir] ? firstSquare(blockers) : lastSquare(blockers);
        if (!(squareBit(s) & attackers)) continue;

        char x = lowerPiece(p.b[s]);
        if (dir < 4) {
            if (x == 'r' || x == 'q') return true;
        } else {
            if (x == 'b' || x == 'q') return true;
        }
    }

    return false;
}

// True when side's king is currently attacked.
bool inCheck(const Position& p, Color side) {
    int ksq = p.king[side];
    if (ksq < 0) return true;
    return squareAttackedBy(p, ksq, other(side));
}

// Apply a move and switch side to move. This also updates en-passant state
// and removes the captured pawn for an en-passant capture.
Position makeMove(const Position& p, const Move& m) {
    const Zobrist& z = zobrist();
    Position q = p;
    char pc = q.b[m.from];
    char piece = lowerPiece(pc);
    bool epCapture = piece == 'p' &&
                     m.to == p.ep &&
                     isEmpty(q.b[m.to]) &&
                     col(m.from) != col(m.to);
    int epCaptureSquare = idx(row(m.from), col(m.to));
    char captured = epCapture ? q.b[epCaptureSquare] : q.b[m.to];
    char placed = m.promo ? makePiece(p.side, m.promo) : pc;
    int capturedSquare = epCapture ? epCaptureSquare : m.to;
    int pcIndex = pieceIndex(pc);
    int placedIndex = pieceIndex(placed);
    assert(pcIndex >= 0);
    assert(placedIndex >= 0);

    if (q.ep >= 0) {
        q.key ^= z.ep[q.ep];
#if TT_DOUBLE_HASH
        q.key2 ^= z.ep2[q.ep];
#endif
    }
    q.key ^= z.piece[pcIndex][m.from];
#if TT_DOUBLE_HASH
    q.key2 ^= z.piece2[pcIndex][m.from];
#endif
    if (!isEmpty(captured)) {
        int capturedIndex = pieceIndex(captured);
        assert(capturedIndex >= 0);
        q.key ^= z.piece[capturedIndex][capturedSquare];
#if TT_DOUBLE_HASH
        q.key2 ^= z.piece2[capturedIndex][capturedSquare];
#endif
    }
    q.key ^= z.piece[placedIndex][m.to];
#if TT_DOUBLE_HASH
    q.key2 ^= z.piece2[placedIndex][m.to];
#endif
    q.key ^= z.blackToMove;
#if TT_DOUBLE_HASH
    q.key2 ^= z.blackToMove2;
#endif

    removeFromBitboards(q, m.from, pc);
    if (!isEmpty(captured)) {
        removeFromBitboards(q, capturedSquare, captured);
    }
    addToBitboards(q, m.to, placed);

    q.b[m.from] = '.';
    if (epCapture) {
        q.b[epCaptureSquare] = '.';
    }
    q.b[m.to] = placed;
    if (piece == 'k') {
        q.king[p.side] = m.to;
    }
    q.side = other(p.side);
    q.ep = -1;

    if (piece == 'p' && abs(row(m.to) - row(m.from)) == 2) {
        int ep = idx((row(m.from) + row(m.to)) / 2, col(m.from));
        if (geom().pawnAttacker[q.side][ep] & q.colorBB[q.side] & q.pieceBB[PAWN]) {
            q.ep = ep;
            q.key ^= z.ep[q.ep];
#if TT_DOUBLE_HASH
            q.key2 ^= z.ep2[q.ep];
#endif
        }
    }

    return q;
}

void addMoveMask(MoveList& moves, int from, uint64_t targets) {
    while (targets) {
        moves.push_back({from, popSquare(targets), 0});
    }
}

void addSlidingMoves(MoveList& moves, const Position& p, int from, int dir) {
    const Geometry& g = geom();
    uint64_t occupied = p.colorBB[WHITE] | p.colorBB[BLACK];
    uint64_t targets = g.ray[dir][from];
    uint64_t blockers = targets & occupied;

    if (blockers) {
        int blocker = g.rayIncreasing[dir] ? firstSquare(blockers) : lastSquare(blockers);
        targets &= ~g.ray[dir][blocker];

        if (sameColor(p.b[blocker], p.side) || isKing(p.b[blocker])) {
            targets &= ~squareBit(blocker);
        }
    }

    addMoveMask(moves, from, targets);
}

// Generate pseudo-legal moves for p.side: piece movement is obeyed, but moves
// that leave p.side in check are filtered later by legalMoves().
void pseudoMoves(const Position& p, MoveList& moves) {
    moves.clear();
    const Geometry& g = geom();
    uint64_t own = p.colorBB[p.side];
    uint64_t enemyNoKing = p.colorBB[other(p.side)] & ~p.pieceBB[KING];
    uint64_t nonKingTargets = BOARD_MASK & ~own & ~p.pieceBB[KING];

    uint64_t pawns = p.colorBB[p.side] & p.pieceBB[PAWN];
    while (pawns) {
        int s = popSquare(pawns);
        int r = row(s), c = col(s);
        int dir = p.side == WHITE ? -1 : 1;
        int rr = r + dir;

        // Single-step pawn push.
        if (inb(rr, c) && isEmpty(p.b[idx(rr, c)])) {
            int to = idx(rr, c);
            bool promote = (p.side == WHITE && rr == 0) ||
                           (p.side == BLACK && rr == H - 1);

            if (promote) {
                for (char pr : {'q', 'r', 'b', 'n'}) {
                    moves.push_back({s, to, pr});
                }
            } else {
                moves.push_back({s, to, 0});
            }

            // Initial two-square pawn push.
            int startRow = p.side == WHITE ? H - 2 : 1;
            int rr2 = r + 2 * dir;
            if (r == startRow && inb(rr2, c) && isEmpty(p.b[idx(rr2, c)])) {
                moves.push_back({s, idx(rr2, c), 0});
            }
        }

        // Captures.
        uint64_t captures = g.pawnAttack[p.side][s] & enemyNoKing;
        while (captures) {
            int to = popSquare(captures);
#if FORBID_TRIVIAL_4X8_WIN
            if (forbidTrivial4x8WinForNextMoveGen &&
                p.side == WHITE &&
                to == idx(1, 5) &&
                (s == idx(H - 2, 4) || s == idx(H - 2, 6))) {
                continue;
            }
#endif
            bool promote = (p.side == WHITE && row(to) == 0) ||
                           (p.side == BLACK && row(to) == H - 1);

            if (promote) {
                for (char pr : {'q', 'r', 'b', 'n'}) {
                    moves.push_back({s, to, pr});
                }
            } else {
                moves.push_back({s, to, 0});
            }
        }

        if (p.ep >= 0 && (g.pawnAttack[p.side][s] & squareBit(p.ep))) {
            int capturedPawn = idx(r, col(p.ep));
            if (isEmpty(p.b[p.ep]) &&
                oppColor(p.b[capturedPawn], p.side) &&
                lowerPiece(p.b[capturedPawn]) == 'p') {
                moves.push_back({s, p.ep, 0});
            }
        }
    }

    uint64_t knights = p.colorBB[p.side] & p.pieceBB[KNIGHT];
    while (knights) {
        int s = popSquare(knights);
        addMoveMask(moves, s, g.knight[s] & nonKingTargets);
    }

    uint64_t kings = p.colorBB[p.side] & p.pieceBB[KING];
    while (kings) {
        int s = popSquare(kings);
        addMoveMask(moves, s, g.king[s] & nonKingTargets);
    }

    uint64_t diagonalSliders = p.colorBB[p.side] & (p.pieceBB[BISHOP] | p.pieceBB[QUEEN]);
    while (diagonalSliders) {
        int s = popSquare(diagonalSliders);
        for (int dir = 4; dir < 8; dir++) {
            addSlidingMoves(moves, p, s, dir);
        }
    }

    uint64_t orthogonalSliders = p.colorBB[p.side] & (p.pieceBB[ROOK] | p.pieceBB[QUEEN]);
    while (orthogonalSliders) {
        int s = popSquare(orthogonalSliders);
        for (int dir = 0; dir < 4; dir++) {
            addSlidingMoves(moves, p, s, dir);
        }
    }
}

// Generate all fully legal moves for p.side.
void legalMoves(const Position& p, MoveList& out) {
    MoveList pseudo;

    out.clear();
    pseudoMoves(p, pseudo);

    for (const Move& m : pseudo) {
        Position q = makeMove(p, m);
        if (!inCheck(q, p.side)) out.push_back(m);
    }
}

MoveList legalMoves(const Position& p) {
    MoveList out;
    legalMoves(p, out);
    return out;
}

#if FORBID_TRIVIAL_4X8_WIN
MoveList legalMovesRoot(const Position& p) {
    forbidTrivial4x8WinForNextMoveGen = true;
    MoveList moves = legalMoves(p);
    forbidTrivial4x8WinForNextMoveGen = false;
    return moves;
}
#else
#define legalMovesRoot legalMoves
#endif

char capturedPiece(const Position& p, const Move& m) {
    char target = p.b[m.to];
    if (!isEmpty(target)) return target;

    char pc = p.b[m.from];
    if (lowerPiece(pc) == 'p' && m.to == p.ep && col(m.from) != col(m.to)) {
        return p.b[idx(row(m.from), col(m.to))];
    }

    return '.';
}

int moveScore(const Position& p, const Move& m, const Position& q,
              bool forcingMove) {
    char pc = p.b[m.from];
    char piece = lowerPiece(pc);
    int score = 0;

    if (inCheck(q, q.side)) {
        score += forcingMove ? 200000 : 80000;
    }

    if (m.promo) {
        score += 100000 + pieceValue(m.promo);
    }

    char captured = capturedPiece(p, m);
    if (!isEmpty(captured)) {
        score += 50000 + 10 * pieceValue(captured) - pieceValue(pc);
    }

    if (!forcingMove && piece == 'k') {
        score += 30000;
    }

    return score;
}

struct SearchMoveList {
    using PositionStorage = aligned_storage_t<sizeof(Position), alignof(Position)>;
    array<PositionStorage, MAX_MOVES> positionStorage;
    array<int, MAX_MOVES> scores{};
    array<int, MAX_MOVES> order{};
    int n = 0;

    ~SearchMoveList() { clear(); }

    Position& position(int i) {
        return *launder(reinterpret_cast<Position*>(&positionStorage[i]));
    }

    const Position& position(int i) const {
        return *launder(reinterpret_cast<const Position*>(&positionStorage[i]));
    }

    void clear() {
        for (int i = 0; i < n; i++) {
            position(i).~Position();
        }
        n = 0;
    }

    bool empty() const { return n == 0; }

    void push_back(const Position& p, int score) {
        assert(n < MAX_MOVES);
        new (&positionStorage[n]) Position(p);
        scores[n] = score;
        order[n] = n;
        n++;
    }

    void sortByScore() {
        sort(order.begin(), order.begin() + n,
             [this](int a, int b) {
                 return scores[a] > scores[b];
             });
    }
};

void legalSearchMoves(const Position& p, SearchMoveList& out,
                      bool forcingMove) {
    MoveList pseudo;

    out.clear();
    pseudoMoves(p, pseudo);

    for (const Move& m : pseudo) {
        Position q = makeMove(p, m);
        if (!inCheck(q, p.side)) {
            out.push_back(q, moveScore(p, m, q, forcingMove));
        }
    }

    out.sortByScore();
}

uint64_t ttContextKey = 0;
bool ttExactDepth = false;

struct TTEntry {
    uint64_t key = 0;
#if TT_DOUBLE_HASH
    uint64_t key2 = 0;
#endif
    uint8_t moves = 0;
    int value = 0;
    int8_t result = 0; // 0 = empty, 1 = false, 2 = true, 3 = exact value.
};

struct TranspositionTable {
    vector<TTEntry> table;
    long long hits = 0;
    long long stores = 0;

    void resizeMB(size_t mb) {
        hits = 0;
        stores = 0;

        size_t bytes = mb * 1024ULL * 1024ULL;
        size_t entries = bytes / sizeof(TTEntry);

        if (mb == 0 || entries == 0) {
            table.clear();
            return;
        }

        table.assign(entries, {});
    }

    bool enabled() const {
        return !table.empty();
    }

    size_t bytes() const {
        return table.size() * sizeof(TTEntry);
    }

    uint64_t cacheKey(const Position& p) const {
        return p.key ^ ttContextKey;
    }

#if TT_DOUBLE_HASH
    uint64_t cacheKey2(const Position& p) const {
        return p.key2 ^ (ttContextKey << 1) ^ (ttContextKey >> 63);
    }
#endif

    bool lookup(const Position& p, int moves, bool& result) {
        if (!enabled()) return false;

        uint64_t key = cacheKey(p);
#if TT_DOUBLE_HASH
        uint64_t key2 = cacheKey2(p);
#endif
        const TTEntry& e = table[key % table.size()];
        if (e.result != 0 &&
            e.key == key &&
#if TT_DOUBLE_HASH
            e.key2 == key2 &&
#endif
            (ttExactDepth
             ? e.moves == moves
             : ((e.result == 2 && e.moves <= moves) ||
                (e.result == 1 && e.moves >= moves)))) {
            hits++;
            result = e.result == 2;
            return true;
        }

        return false;
    }

    bool lookupValue(const Position& p, int moves, int& value) {
        if (!enabled()) return false;

        uint64_t key = cacheKey(p);
#if TT_DOUBLE_HASH
        uint64_t key2 = cacheKey2(p);
#endif
        const TTEntry& e = table[key % table.size()];
        if (e.result == 3 &&
            e.key == key &&
#if TT_DOUBLE_HASH
            e.key2 == key2 &&
#endif
            e.moves == moves) {
            hits++;
            value = e.value;
            return true;
        }

        return false;
    }

    void store(const Position& p, int moves, bool result) {
        if (!enabled()) return;

        uint64_t key = cacheKey(p);
#if TT_DOUBLE_HASH
        uint64_t key2 = cacheKey2(p);
#endif
        TTEntry& e = table[key % table.size()];
        if (!ttExactDepth &&
            e.result != 0 &&
            e.key == key &&
#if TT_DOUBLE_HASH
            e.key2 == key2 &&
#endif
            e.moves != moves &&
            ((e.result == 2 && e.moves <= moves && result) ||
             (e.result == 1 && e.moves >= moves && !result))) {
            return;
        }
        if (e.result != 0 &&
            (e.key != key ||
#if TT_DOUBLE_HASH
             e.key2 != key2 ||
#endif
             e.moves != moves) &&
            e.moves > moves) {
            return;
        }

        e.key = key;
#if TT_DOUBLE_HASH
        e.key2 = key2;
#endif
        e.moves = uint8_t(moves);
        e.result = result ? 2 : 1;
        stores++;
    }

    void storeValue(const Position& p, int moves, int value) {
        if (!enabled()) return;

        uint64_t key = cacheKey(p);
#if TT_DOUBLE_HASH
        uint64_t key2 = cacheKey2(p);
#endif
        TTEntry& e = table[key % table.size()];
        if (e.result != 0 &&
            (e.key != key ||
#if TT_DOUBLE_HASH
             e.key2 != key2 ||
#endif
             e.moves != moves) &&
            e.moves > moves) {
            return;
        }

        e.key = key;
#if TT_DOUBLE_HASH
        e.key2 = key2;
#endif
        e.moves = uint8_t(moves);
        e.value = value;
        e.result = 3;
        stores++;
    }
};

TranspositionTable tt;

// True when the side to move is in check and has no legal moves.
bool isCheckmate(const Position& p) {
    if (!inCheck(p, p.side)) return false;
    MoveList moves;
    legalMoves(p, moves);
    return moves.empty();
}

enum class GoalKind { Mate, MaterialThreshold, MaterialValue };

struct SearchGoal {
    GoalKind kind = GoalKind::Mate;
    int materialThreshold = 0;
};

SearchGoal currentGoal;
Color currentAttacker = WHITE;
static constexpr int MATERIAL_INF = 1000000000;

void setCurrentAttacker(Color attacker) {
    currentAttacker = attacker;
    ttExactDepth = currentGoal.kind == GoalKind::MaterialThreshold;
    if (currentGoal.kind != GoalKind::Mate) {
        ttContextKey = attacker == WHITE ? 0x5f7c2e8d9a4316b0ULL
                                         : 0xb8d13a64c2f09e57ULL;
    } else {
        ttContextKey = 0;
    }
}

int materialBalance(const Position& p, Color side) {
    int balance = 0;

    for (char pc : p.b) {
        if (isEmpty(pc)) continue;
        int value = pieceValue(pc);
        if (sameColor(pc, side)) {
            balance += value;
        } else {
            balance -= value;
        }
    }

    return balance;
}

string goalDescription(const SearchGoal& goal) {
    if (goal.kind == GoalKind::Mate) return "mate";
    if (goal.kind == GoalKind::MaterialValue) return "max material";

    ostringstream out;
    out << "material >= " << goal.materialThreshold;
    return out.str();
}

bool materialGoalReached(const Position& p, Color attacker) {
    return materialBalance(p, attacker) >= currentGoal.materialThreshold;
}

string materialValueDescription(int value) {
    if (value >= MATERIAL_INF) return "+infinity";
    if (value <= -MATERIAL_INF) return "-infinity";
    return to_string(value);
}

// Convert a board index to algebraic square notation such as "e4".
string sqName(int s) {
    string out;
    out.push_back(char('a' + col(s)));
    out.push_back(char('1' + (H - 1 - row(s))));
    return out;
}

// Convert a move to long algebraic coordinate notation, e.g. "e2e4" or
// "a4a5q" for promotion.
string moveName(const Move& m) {
    string s = sqName(m.from) + sqName(m.to);
    if (m.promo) s.push_back(m.promo);
    return s;
}

// Side-to-move can force the current goal within this many own moves.
// For mate, success can occur as soon as a checking move leaves no replies.
// For material, all reply branches are searched to the horizon, where the
// material balance is tested for currentAttacker.
bool forceGoalInMoves(const Position& p, int moves, long long& nodes) {
    nodes++;

    // p.side is the side trying to force the goal at this node.
    if (moves <= 0) {
        return currentGoal.kind == GoalKind::MaterialThreshold &&
               materialGoalReached(p, currentAttacker);
    }

    bool cached = false;
    if (tt.lookup(p, moves, cached)) {
        return cached;
    }

    SearchMoveList myMoves;
    legalSearchMoves(p, myMoves, true);
    if (myMoves.empty()) {
        tt.store(p, moves, false);
        return false;
    }

    for (int i = 0; i < myMoves.n; i++) {
        const Position& q = myMoves.position(myMoves.order[i]);

        SearchMoveList replies;
        legalSearchMoves(q, replies, false);

        // Stalemate or no legal replies but not mate is not success.
        if (replies.empty()) {
            if (inCheck(q, q.side)) {
                tt.store(p, moves, true);
                return true;
            }
            continue;
        }

        bool worksAgainstEveryReply = true;

        for (int j = 0; j < replies.n; j++) {
            const Position& afterReply = replies.position(replies.order[j]);

            if (!forceGoalInMoves(afterReply, moves - 1, nodes)) {
                worksAgainstEveryReply = false;
                break;
            }
        }

        if (worksAgainstEveryReply) {
            tt.store(p, moves, true);
            return true;
        }
    }

    tt.store(p, moves, false);
    return false;
}

#if FORBID_TRIVIAL_4X8_WIN
// Special top-level search entry point. When the trivial 4x8 filter is enabled,
// only this root ply omits the two immediate winning moves; recursive calls use
// forceGoalInMoves() so the normal position cache and legal move set still
// apply everywhere below the root.
bool forceGoalInMovesRoot(const Position& p, int moves, long long& nodes) {
    nodes++;

    if (moves <= 0) {
        return currentGoal.kind == GoalKind::MaterialThreshold &&
               materialGoalReached(p, currentAttacker);
    }

    SearchMoveList myMoves;
    forbidTrivial4x8WinForNextMoveGen = true;
    legalSearchMoves(p, myMoves, true);
    forbidTrivial4x8WinForNextMoveGen = false;

    if (myMoves.empty()) return false;

    for (int i = 0; i < myMoves.n; i++) {
        const Position& q = myMoves.position(myMoves.order[i]);

        SearchMoveList replies;
        legalSearchMoves(q, replies, false);

        if (replies.empty()) {
            if (inCheck(q, q.side)) return true;
            continue;
        }

        bool worksAgainstEveryReply = true;

        for (int j = 0; j < replies.n; j++) {
            const Position& afterReply = replies.position(replies.order[j]);

            if (!forceGoalInMoves(afterReply, moves - 1, nodes)) {
                worksAgainstEveryReply = false;
                break;
            }
        }

        if (worksAgainstEveryReply) return true;
    }

    return false;
}
#else
#define forceGoalInMovesRoot forceGoalInMoves
#endif

int materialValueInMoves(const Position& p, int moves, int alpha, int beta,
                         long long& nodes) {
    nodes++;

    if (moves <= 0) return materialBalance(p, currentAttacker);

    int cached = 0;
    if (tt.lookupValue(p, moves, cached)) {
        return cached;
    }

    SearchMoveList myMoves;
    legalSearchMoves(p, myMoves, true);
    if (myMoves.empty()) {
        int value = inCheck(p, p.side)
                    ? -MATERIAL_INF
                    : materialBalance(p, currentAttacker);
        tt.storeValue(p, moves, value);
        return value;
    }

    int best = -MATERIAL_INF;
    bool exact = true;
    for (int i = 0; i < myMoves.n; i++) {
        const Position& q = myMoves.position(myMoves.order[i]);

        SearchMoveList replies;
        legalSearchMoves(q, replies, false);

        int moveValue = MATERIAL_INF;
        if (replies.empty()) {
            moveValue = inCheck(q, q.side)
                        ? MATERIAL_INF
                        : materialBalance(q, currentAttacker);
        } else {
            for (int j = 0; j < replies.n; j++) {
                const Position& afterReply =
                    replies.position(replies.order[j]);
                int value = materialValueInMoves(afterReply, moves - 1,
                                                 alpha, moveValue,
                                                 nodes);
                moveValue = min(moveValue, value);
                if (moveValue <= alpha) {
                    exact = false;
                    break;
                }
            }
        }

        best = max(best, moveValue);
        alpha = max(alpha, best);
        if (alpha >= beta) {
            exact = false;
            break;
        }
    }

    if (exact) tt.storeValue(p, moves, best);
    return best;
}

#if FORBID_TRIVIAL_4X8_WIN
int materialValueInMovesRoot(const Position& p, int moves, int alpha, int beta,
                             long long& nodes) {
    nodes++;

    if (moves <= 0) return materialBalance(p, currentAttacker);

    SearchMoveList myMoves;
    forbidTrivial4x8WinForNextMoveGen = true;
    legalSearchMoves(p, myMoves, true);
    forbidTrivial4x8WinForNextMoveGen = false;

    if (myMoves.empty()) {
        return inCheck(p, p.side)
               ? -MATERIAL_INF
               : materialBalance(p, currentAttacker);
    }

    int best = -MATERIAL_INF;
    for (int i = 0; i < myMoves.n; i++) {
        const Position& q = myMoves.position(myMoves.order[i]);

        SearchMoveList replies;
        legalSearchMoves(q, replies, false);

        int moveValue = MATERIAL_INF;
        if (replies.empty()) {
            moveValue = inCheck(q, q.side)
                        ? MATERIAL_INF
                        : materialBalance(q, currentAttacker);
        } else {
            for (int j = 0; j < replies.n; j++) {
                const Position& afterReply =
                    replies.position(replies.order[j]);
                int value = materialValueInMoves(afterReply, moves - 1,
                                                 alpha, moveValue,
                                                 nodes);
                moveValue = min(moveValue, value);
                if (moveValue <= alpha) break;
            }
        }

        best = max(best, moveValue);
        alpha = max(alpha, best);
        if (alpha >= beta) break;
    }

    return best;
}
#else
#define materialValueInMovesRoot materialValueInMoves
#endif

void printIndent(int n) {
    for (int i = 0; i < n; i++) cout << ' ';
}

void printBoard(const Position& p, int indent = 0);

void printStrategyBoard(const Position& p, int indent) {
    printBoard(p, indent);
    cout << "\n";
}

bool moveForcesMate(const Position& p, const Move& m, int moves) {
    Position q = makeMove(p, m);
    MoveList replies = legalMoves(q);

    if (replies.empty()) return inCheck(q, q.side);

    for (const Move& reply : replies) {
        Position afterReply = makeMove(q, reply);
        long long nodes = 0;
        if (!forceGoalInMoves(afterReply, moves - 1, nodes)) {
            return false;
        }
    }

    return true;
}

// Print one forcing strategy tree for the side to move. This is used for small
// top-level White mate reports: the root can have special first-move filtering,
// while recursive calls use the normal legal move set and search.
int printMateStrategy(const Position& p, int moves, int moveNumber, int indent,
                      bool root = false) {
    MoveList myMoves = root ? legalMovesRoot(p) : legalMoves(p);
    int strategies = 0;

    for (const Move& wm : myMoves) {
        if (!moveForcesMate(p, wm, moves)) continue;
        strategies++;

        Position q = makeMove(p, wm);
        MoveList replies = legalMoves(q);

        printIndent(indent);
        cout << moveNumber << ". White " << moveName(wm);
        if (replies.empty()) {
            cout << "#\n";
            printStrategyBoard(q, indent + 2);
            if (!root) return 1;
            continue;
        }
        cout << " (Black has " << replies.n << " legal "
             << (replies.n == 1 ? "reply" : "replies") << ")\n";
        printStrategyBoard(q, indent + 2);

        for (const Move& reply : replies) {
            Position afterReply = makeMove(q, reply);
            printIndent(indent + 2);
            cout << "if Black " << moveName(reply) << ":\n";
            printStrategyBoard(afterReply, indent + 4);
            if (printMateStrategy(afterReply, moves - 1,
                                  moveNumber + 1, indent + 4) == 0) {
                printIndent(indent + 4);
                cout << "(strategy not found)\n";
            }
        }

        if (!root) return 1;
    }

    return strategies;
}

// Print whether White can force the configured goal from the initial position
// in n White moves.
bool whiteCanForceGoalIn(const Position& start, int n) {
    Position p = start;
    p.side = WHITE;
    computeKeys(p);
    setCurrentAttacker(WHITE);

    long long nodes = 0;
    bool ans = forceGoalInMovesRoot(p, n, nodes);

    cout << "White " << goalDescription(currentGoal) << " in " << n << ": "
         << (ans ? "YES" : "no")
         << ", nodes=" << nodes << "\n";

    if (currentGoal.kind == GoalKind::Mate && ans && n <= 3) {
        cout << "\nWhite mate-in-" << n << " strategy:\n";
        int strategies = printMateStrategy(p, n, 1, 0, true);
        if (strategies == 0) {
            cout << "(strategy not found)\n";
        } else {
            cout << "Winning first moves: " << strategies << "\n";
        }
    }

    return ans;
}

int whiteMaterialValueIn(const Position& start, int n) {
    Position p = start;
    p.side = WHITE;
    computeKeys(p);
    setCurrentAttacker(WHITE);

    long long nodes = 0;
    int value = materialValueInMovesRoot(p, n, -MATERIAL_INF, MATERIAL_INF,
                                         nodes);

    cout << "White max material in " << n << ": "
         << materialValueDescription(value)
         << ", nodes=" << nodes << "\n";

    return value;
}

// This answers:
// After White's first move, can Black force the configured goal in n Black moves
// no matter which first move White chooses?
bool blackCanForceGoalAfterWhiteMove(const Position& start, int n) {
    Position p = start;
    p.side = WHITE;
    computeKeys(p);

    MoveList whiteFirstMoves = legalMovesRoot(p);

    bool blackForcesAfterAllWhiteMoves = true;

    cout << "\nTesting Black " << goalDescription(currentGoal) << " in " << n
         << " after White's first move:\n";

    for (const Move& wm : whiteFirstMoves) {
        Position afterWhite = makeMove(p, wm);
        setCurrentAttacker(BLACK);

        long long nodes = 0;
        bool blackForces = forceGoalInMoves(afterWhite, n, nodes);

        cout << "1. " << moveName(wm)
             << " : "
             << (blackForces
                 ? (currentGoal.kind == GoalKind::Mate ? "Black mates"
                                                       : "Black reaches goal")
                 : "White escapes")
             << ", nodes=" << nodes << "\n";

        if (!blackForces) {
            blackForcesAfterAllWhiteMoves = false;
            break;
        }
    }

    cout << "Overall Black " << goalDescription(currentGoal) << " in " << n << ": "
         << (blackForcesAfterAllWhiteMoves ? "YES" : "no")
         << "\n";

    return blackForcesAfterAllWhiteMoves;
}

int blackMaterialValueAfterWhiteMove(const Position& start, int n) {
    Position p = start;
    p.side = WHITE;
    computeKeys(p);

    MoveList whiteFirstMoves = legalMovesRoot(p);

    int overall = MATERIAL_INF;

    cout << "\nTesting Black max material in " << n
         << " after White's first move:\n";

    for (const Move& wm : whiteFirstMoves) {
        Position afterWhite = makeMove(p, wm);
        setCurrentAttacker(BLACK);

        long long nodes = 0;
        int value = materialValueInMoves(afterWhite, n, -MATERIAL_INF,
                                         MATERIAL_INF, nodes);
        overall = min(overall, value);

        cout << "1. " << moveName(wm)
             << " : Black guarantees " << materialValueDescription(value)
             << ", nodes=" << nodes << "\n";
    }

    cout << "Overall Black max material in " << n << ": "
         << materialValueDescription(overall) << "\n";

    return overall;
}

// Print the board with ranks 1..H and files a..h.
void printBoard(const Position& p, int indent) {
    for (int r = 0; r < H; r++) {
        printIndent(indent);
        cout << H - r << "  ";
        for (int c = 0; c < W; c++) {
            cout << p.b[idx(r, c)] << ' ';
        }
        cout << "\n";
    }
    printIndent(indent);
    cout << "   ";
    for (int c = 0; c < W; c++) {
        cout << char('a' + c) << ' ';
    }
    cout << "\n";
}

struct DepthRange {
    int first = 1;
    int last = 5;
};

bool parseDepth(const string& s, int& out) {
    if (s.empty()) return false;

    int value = 0;
    for (char ch : s) {
        if (!isdigit(static_cast<unsigned char>(ch))) return false;
        value = value * 10 + (ch - '0');
        if (value > 255) return false;
    }

    if (value < 1) return false;
    out = value;
    return true;
}

bool parseDepthRange(const string& spec, DepthRange& range) {
    size_t dots = spec.find("..");
    if (dots == string::npos) {
        int n = 0;
        if (!parseDepth(spec, n)) return false;
        range.first = n;
        range.last = n;
        return true;
    }

    if (spec.find("..", dots + 2) != string::npos) return false;

    int first = 0;
    int last = 0;
    if (!parseDepth(spec.substr(0, dots), first) ||
        !parseDepth(spec.substr(dots + 2), last) ||
        first > last) {
        return false;
    }

    range.first = first;
    range.last = last;
    return true;
}

bool parseNonNegativeInt(const string& s, int& out) {
    if (s.empty()) return false;

    int value = 0;
    for (char ch : s) {
        if (!isdigit(static_cast<unsigned char>(ch))) return false;
        int digit = ch - '0';
        if (value > (numeric_limits<int>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }

    out = value;
    return true;
}

bool parseSize(const string& s, size_t& out) {
    if (s.empty()) return false;

    size_t value = 0;
    for (char ch : s) {
        if (!isdigit(static_cast<unsigned char>(ch))) return false;
        size_t digit = static_cast<size_t>(ch - '0');
        if (value > (numeric_limits<size_t>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }

    out = value;
    return true;
}

bool parseGoal(const string& spec, SearchGoal& goal) {
    if (spec == "mate") {
        goal = {};
        return true;
    }

    if (spec == "material") {
        goal.kind = GoalKind::MaterialValue;
        goal.materialThreshold = 0;
        return true;
    }

    const string prefix = "material:";
    if (spec.rfind(prefix, 0) == 0) {
        int threshold = 0;
        if (!parseNonNegativeInt(spec.substr(prefix.size()), threshold)) {
            return false;
        }
        goal.kind = GoalKind::MaterialThreshold;
        goal.materialThreshold = threshold;
        return true;
    }

    return false;
}

struct ProgramOptions {
    DepthRange depths;
    size_t tt = 8;
};

void printUsage(const char* prog, ostream& out = cerr) {
    out << "usage: " << prog << " [options]\n"
        << "  -d, --depth N|A..B       own-move depth range, within 1..255"
        << " (default 1..5)\n"
        << "  -t, --tt MB              transposition table size in MB"
        << " (default 8; 0 disables)\n"
        << "  -g, --goal GOAL          mate, material, or material:K"
        << " (default mate)\n"
        << "  -w, --weights NAME       shannon, turing, coxeter, or kaufman"
        << " (default shannon)\n"
        << "  -h, --help               show this help\n"
        << "  material K is measured in the selected centipawn weight scale\n"
        << "  build with OPTIONS=-DBOARD_RANKS=N for N in 4..8, default 5\n"
        << "  add -DFORBID_TRIVIAL_4X8_WIN=1 to omit e2f3/g2f3 as 4x8 first moves\n"
        << "  examples: " << prog << " -d 5 -t 8, "
        << prog << " --depth 1..5 --goal material --weights shannon\n";
}

bool optionNeedsValue(const char* prog, const string& option, int argc,
                      char** argv, int& i, string& value) {
    if (i + 1 >= argc) {
        cerr << "error: missing value for " << option << "\n";
        printUsage(prog);
        return false;
    }

    value = argv[++i];
    return true;
}

bool parseOptions(int argc, char** argv, ProgramOptions& options,
                  bool& helpRequested) {
    helpRequested = false;

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        string value;

        if (arg == "-h" || arg == "--help") {
            helpRequested = true;
            return true;
        } else if (arg == "-d" || arg == "--depth") {
            if (!optionNeedsValue(argv[0], arg, argc, argv, i, value)) {
                return false;
            }
            if (!parseDepthRange(value, options.depths)) {
                cerr << "error: invalid depth range '" << value << "'\n";
                printUsage(argv[0]);
                return false;
            }
        } else if (arg.rfind("--depth=", 0) == 0) {
            value = arg.substr(8);
            if (!parseDepthRange(value, options.depths)) {
                cerr << "error: invalid depth range '" << value << "'\n";
                printUsage(argv[0]);
                return false;
            }
        } else if (arg == "-t" || arg == "--tt") {
            if (!optionNeedsValue(argv[0], arg, argc, argv, i, value)) {
                return false;
            }
            if (!parseSize(value, options.tt)) {
                cerr << "error: invalid transposition table size '" << value
                     << "'\n";
                printUsage(argv[0]);
                return false;
            }
        } else if (arg.rfind("--tt=", 0) == 0) {
            value = arg.substr(5);
            if (!parseSize(value, options.tt)) {
                cerr << "error: invalid transposition table size '" << value
                     << "'\n";
                printUsage(argv[0]);
                return false;
            }
        } else if (arg == "-g" || arg == "--goal") {
            if (!optionNeedsValue(argv[0], arg, argc, argv, i, value)) {
                return false;
            }
            if (!parseGoal(value, currentGoal)) {
                cerr << "error: invalid goal '" << value << "'\n";
                printUsage(argv[0]);
                return false;
            }
        } else if (arg.rfind("--goal=", 0) == 0) {
            value = arg.substr(7);
            if (!parseGoal(value, currentGoal)) {
                cerr << "error: invalid goal '" << value << "'\n";
                printUsage(argv[0]);
                return false;
            }
        } else if (arg == "-w" || arg == "--weights") {
            if (!optionNeedsValue(argv[0], arg, argc, argv, i, value)) {
                return false;
            }
            if (!parseWeights(value, currentWeights)) {
                cerr << "error: invalid weights '" << value << "'\n";
                printUsage(argv[0]);
                return false;
            }
        } else if (arg.rfind("--weights=", 0) == 0) {
            value = arg.substr(10);
            if (!parseWeights(value, currentWeights)) {
                cerr << "error: invalid weights '" << value << "'\n";
                printUsage(argv[0]);
                return false;
            }
        } else {
            cerr << "error: unexpected positional argument or option '" << arg
                 << "'\n";
            printUsage(argv[0]);
            return false;
        }
    }

    return true;
}

// Run the configured goal search for one depth or an inclusive range of depths.
int main(int argc, char** argv) {
    ProgramOptions options;
    bool helpRequested = false;
    if (!parseOptions(argc, argv, options, helpRequested)) {
        return 1;
    }

    if (helpRequested) {
        printUsage(argv[0], cout);
        return 0;
    }

    Position start = initialPosition();

    printBoard(start);
    tt.resizeMB(options.tt);

    cout << "\nGoal: " << goalDescription(currentGoal)
         << "\nWeights: " << weightsDescription(currentWeights) << "\n";

    cout << fixed << setprecision(2)
         << "\nTransposition table: "
         << (double(tt.bytes()) / (1024 * 1024)) << " MB, "
         << tt.table.size() << " entries\n"
         << defaultfloat;

    cout << "\nLegal White first moves:\n";
    MoveList firstMoves = legalMovesRoot(start);
    for (const Move& m : firstMoves) {
        cout << moveName(m) << " ";
    }
    cout << "\n\n";

    if (currentGoal.kind == GoalKind::MaterialValue) {
        cout << "=== White max material ===\n";
        for (int n = options.depths.first; n <= options.depths.last; n++) {
            whiteMaterialValueIn(start, n);
        }

        cout << "\n=== Black max material after White's first move ===\n";
        for (int n = options.depths.first; n <= options.depths.last; n++) {
            blackMaterialValueAfterWhiteMove(start, n);
        }
    } else {
        cout << "=== White forced " << goalDescription(currentGoal) << " ===\n";
        for (int n = options.depths.first; n <= options.depths.last; n++) {
            whiteCanForceGoalIn(start, n);
        }

        cout << "\n=== Black forced " << goalDescription(currentGoal)
             << " after White's first move ===\n";
        for (int n = options.depths.first; n <= options.depths.last; n++) {
            blackCanForceGoalAfterWhiteMove(start, n);
        }
    }

    cout << "\nTT hits=" << tt.hits
         << ", stores=" << tt.stores << "\n";

    return 0;
}
