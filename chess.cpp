#include <bits/stdc++.h>
using namespace std;

static constexpr int H = 5;
static constexpr int W = 8;
static constexpr int N_SQUARES = H * W;
static constexpr uint64_t BOARD_MASK = (1ULL << N_SQUARES) - 1;

#ifndef TT_DOUBLE_HASH
#define TT_DOUBLE_HASH 1
#endif

enum Color { WHITE = 0, BLACK = 1 };
enum PieceType { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING };

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

// True when a row/column coordinate is on the 5x8 board.
bool inb(int r, int c) {
    return r >= 0 && r < H && c >= 0 && c < W;
}

// Piece and color classification helpers.
bool isWhite(char p) { return p >= 'A' && p <= 'Z'; }
bool isBlack(char p) { return p >= 'a' && p <= 'z'; }
bool isEmpty(char p) { return p == '.'; }
bool isKing(char p) { return p == 'K' || p == 'k'; }
char lowerPiece(char p) { return isWhite(p) ? char(p - 'A' + 'a') : p; }

int pieceValue(char p) {
    switch (lowerPiece(p)) {
        case 'q': return 900;
        case 'r': return 500;
        case 'b': return 325;
        case 'n': return 300;
        case 'p': return 100;
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

// Build the short-chess starting position.
Position initial5x8() {
    Position p;
    p.b.fill('.');

    string blackBack = "rnbqkbnr";
    string blackPawns = "pppppppp";
    string whitePawns = "PPPPPPPP";
    string whiteBack = "RNBQKBNR";

    for (int c = 0; c < W; c++) {
        p.b[idx(0, c)] = blackBack[c];
        p.b[idx(1, c)] = blackPawns[c];
        p.b[idx(2, c)] = '.';
        p.b[idx(3, c)] = whitePawns[c];
        p.b[idx(4, c)] = whiteBack[c];
    }

    p.side = WHITE;
    p.king[WHITE] = idx(4, 4);
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

char capturedPiece(const Position& p, const Move& m) {
    char target = p.b[m.to];
    if (!isEmpty(target)) return target;

    char pc = p.b[m.from];
    if (lowerPiece(pc) == 'p' && m.to == p.ep && col(m.from) != col(m.to)) {
        return p.b[idx(row(m.from), col(m.to))];
    }

    return '.';
}

int moveScore(const Position& p, const Move& m, const Position& q, bool forcingMove) {
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

void legalSearchMoves(const Position& p, SearchMoveList& out, bool forcingMove) {
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

struct TTEntry {
    uint64_t key = 0;
#if TT_DOUBLE_HASH
    uint64_t key2 = 0;
#endif
    uint8_t moves = 0;
    int8_t result = 0; // 0 = empty, 1 = false, 2 = true.
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

    uint64_t cacheKey(uint64_t positionKey) const {
        return positionKey;
    }

    bool lookup(const Position& p, int moves, bool& result) {
        if (!enabled()) return false;

        const TTEntry& e = table[cacheKey(p.key) % table.size()];
        if (e.result != 0 &&
            e.key == p.key &&
#if TT_DOUBLE_HASH
            e.key2 == p.key2 &&
#endif
            ((e.result == 2 && e.moves <= moves) ||
             (e.result == 1 && e.moves >= moves))) {
            hits++;
            result = e.result == 2;
            return true;
        }

        return false;
    }

    void store(const Position& p, int moves, bool result) {
        if (!enabled()) return;

        TTEntry& e = table[cacheKey(p.key) % table.size()];
        if (e.result != 0 &&
            e.key == p.key &&
#if TT_DOUBLE_HASH
            e.key2 == p.key2 &&
#endif
            e.moves != moves &&
            ((e.result == 2 && e.moves <= moves && result) ||
             (e.result == 1 && e.moves >= moves && !result))) {
            return;
        }
        if (e.result != 0 &&
            (e.key != p.key ||
#if TT_DOUBLE_HASH
             e.key2 != p.key2 ||
#endif
             e.moves != moves) &&
            e.moves > moves) {
            return;
        }

        e.key = p.key;
#if TT_DOUBLE_HASH
        e.key2 = p.key2;
#endif
        e.moves = uint8_t(moves);
        e.result = result ? 2 : 1;
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

// Side-to-move can force mate within this many own moves.
// Example: moves=3 means move, reply, move, reply, move mate.
bool forceMateInMoves(const Position& p, int moves, long long& nodes) {
    nodes++;

    // p.side is the side trying to force mate at this node.
    if (moves <= 0) return false;

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

            if (!forceMateInMoves(afterReply, moves - 1, nodes)) {
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

// Print whether White can force mate from the initial position in n White moves.
bool whiteCanForceMateIn(const Position& start, int n) {
    Position p = start;
    p.side = WHITE;
    computeKeys(p);

    long long nodes = 0;
    bool ans = forceMateInMoves(p, n, nodes);

    cout << "White mate in " << n << ": "
         << (ans ? "YES" : "no")
         << ", nodes=" << nodes << "\n";

    return ans;
}

// This answers:
// After White's first move, can Black force mate in n Black moves
// no matter which first move White chooses?
bool blackCanForceMateAfterWhiteMove(const Position& start, int n) {
    Position p = start;
    p.side = WHITE;
    computeKeys(p);

    MoveList whiteFirstMoves = legalMoves(p);

    bool blackForcesAfterAllWhiteMoves = true;

    cout << "\nTesting Black mate in " << n
         << " after White's first move:\n";

    for (const Move& wm : whiteFirstMoves) {
        Position afterWhite = makeMove(p, wm);

        long long nodes = 0;
        bool blackForces = forceMateInMoves(afterWhite, n, nodes);

        cout << "1. " << moveName(wm)
             << " : " << (blackForces ? "Black mates" : "White escapes")
             << ", nodes=" << nodes << "\n";

        if (!blackForces) {
            blackForcesAfterAllWhiteMoves = false;
        }
    }

    cout << "Overall Black mate in " << n << ": "
         << (blackForcesAfterAllWhiteMoves ? "YES" : "no")
         << "\n";

    return blackForcesAfterAllWhiteMoves;
}

// Print the board with short-chess ranks 1..5 and files a..h.
void printBoard(const Position& p) {
    for (int r = 0; r < H; r++) {
        cout << H - r << "  ";
        for (int c = 0; c < W; c++) {
            cout << p.b[idx(r, c)] << ' ';
        }
        cout << "\n";
    }
    cout << "   a b c d e f g h\n";
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

void printUsage(const char* prog) {
    cerr << "usage: " << prog << " [depth|first..last] [transposition-table-mb]\n"
         << "  depth range must be within 1..255\n"
         << "  examples: " << prog << " 5, " << prog << " 1..5 8\n";
}

// Run the mate search for one depth or an inclusive range of depths.
// argv[2] optionally sets the transposition table size in MB, defaulting to 8.
int main(int argc, char** argv) {
    if (argc > 3) {
        printUsage(argv[0]);
        return 1;
    }

    DepthRange depths;
    if (argc >= 2) {
        if (!parseDepthRange(argv[1], depths)) {
            cerr << "error: invalid depth range '" << argv[1] << "'\n";
            printUsage(argv[0]);
            return 1;
        }
    }
    if (depths.last > 255) {
        cerr << "error: depth must be <= 255\n";
        return 1;
    }

    size_t ttMB = 8;
    if (argc >= 3) {
        ttMB = strtoull(argv[2], nullptr, 10);
    }

    Position start = initial5x8();

    printBoard(start);
    tt.resizeMB(ttMB);

    cout << fixed << setprecision(2)
         << "\nTransposition table: "
         << (double(tt.bytes()) / (1024 * 1024)) << " MB, "
         << tt.table.size() << " entries\n"
         << defaultfloat;

    cout << "\nLegal White first moves:\n";
    for (const Move& m : legalMoves(start)) {
        cout << moveName(m) << " ";
    }
    cout << "\n\n";

    cout << "=== White forced mates ===\n";
    for (int n = depths.first; n <= depths.last; n++) {
        whiteCanForceMateIn(start, n);
    }

    cout << "\n=== Black forced mates after White's first move ===\n";
    for (int n = depths.first; n <= depths.last; n++) {
        blackCanForceMateAfterWhiteMove(start, n);
    }

    cout << "\nTT hits=" << tt.hits
         << ", stores=" << tt.stores << "\n";

    return 0;
}
