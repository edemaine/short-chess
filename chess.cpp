#include <bits/stdc++.h>
using namespace std;

static constexpr int H = 5;
static constexpr int W = 8;

enum Color { WHITE = 0, BLACK = 1 };

// A move is encoded by source and destination square indices plus optional
// promotion piece, stored as lowercase q/r/b/n independent of side.
struct Move {
    int from;
    int to;
    char promo; // 0, 'q', 'r', 'b', 'n'
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
    array<int, 2> king = {-1, -1};
    uint64_t key = 0;
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

int pieceIndex(char p) {
    int base = isWhite(p) ? 0 : 6;

    switch (lowerPiece(p)) {
        case 'p': return base + 0;
        case 'n': return base + 1;
        case 'b': return base + 2;
        case 'r': return base + 3;
        case 'q': return base + 4;
        case 'k': return base + 5;
        default: return -1;
    }
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
    array<uint64_t, H * W> ep{};
    uint64_t blackToMove = 0;

    Zobrist() {
        uint64_t seed = 0x0c0ffee123456789ULL;
        for (auto& pieceSquares : piece) {
            for (uint64_t& x : pieceSquares) {
                x = splitmix64(seed);
            }
        }
        for (uint64_t& x : ep) {
            x = splitmix64(seed);
        }
        blackToMove = splitmix64(seed);
    }
};

const Zobrist& zobrist() {
    static const Zobrist z;
    return z;
}

uint64_t computeKey(const Position& p) {
    const Zobrist& z = zobrist();
    uint64_t key = 0;

    for (int s = 0; s < H * W; s++) {
        int pi = pieceIndex(p.b[s]);
        if (pi >= 0) key ^= z.piece[pi][s];
    }

    if (p.side == BLACK) key ^= z.blackToMove;
    if (p.ep >= 0) key ^= z.ep[p.ep];

    return key;
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
    p.key = computeKey(p);
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
    int r = row(sq), c = col(sq);

    // Pawns.
    if (attacker == WHITE) {
        for (int dc : {-1, 1}) {
            int rr = r + 1;
            int cc = c + dc;
            if (inb(rr, cc) && p.b[idx(rr, cc)] == 'P') return true;
        }
    } else {
        for (int dc : {-1, 1}) {
            int rr = r - 1;
            int cc = c + dc;
            if (inb(rr, cc) && p.b[idx(rr, cc)] == 'p') return true;
        }
    }

    // Knights.
    static const int knightD[8][2] = {
        {1,2},{2,1},{2,-1},{1,-2},
        {-1,-2},{-2,-1},{-2,1},{-1,2}
    };

    char n = attacker == WHITE ? 'N' : 'n';
    for (auto& d : knightD) {
        int rr = r + d[0], cc = c + d[1];
        if (inb(rr, cc) && p.b[idx(rr, cc)] == n) return true;
    }

    // King.
    char k = attacker == WHITE ? 'K' : 'k';
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            int rr = r + dr, cc = c + dc;
            if (inb(rr, cc) && p.b[idx(rr, cc)] == k) return true;
        }
    }

    char rook = attacker == WHITE ? 'R' : 'r';
    char bishop = attacker == WHITE ? 'B' : 'b';
    char queen = attacker == WHITE ? 'Q' : 'q';

    auto ray = [&](int dr, int dc, char slider) -> bool {
        int rr = r + dr, cc = c + dc;
        while (inb(rr, cc)) {
            char x = p.b[idx(rr, cc)];
            if (!isEmpty(x)) return x == slider || x == queen;
            rr += dr;
            cc += dc;
        }
        return false;
    };

    if (ray(1, 0, rook) || ray(-1, 0, rook) ||
        ray(0, 1, rook) || ray(0, -1, rook)) return true;

    if (ray(1, 1, bishop) || ray(1, -1, bishop) ||
        ray(-1, 1, bishop) || ray(-1, -1, bishop)) return true;

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

    if (q.ep >= 0) q.key ^= z.ep[q.ep];
    q.key ^= z.piece[pieceIndex(pc)][m.from];
    if (!isEmpty(captured)) {
        q.key ^= z.piece[pieceIndex(captured)][epCapture ? epCaptureSquare : m.to];
    }
    q.key ^= z.piece[pieceIndex(placed)][m.to];
    q.key ^= z.blackToMove;

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
        q.ep = idx((row(m.from) + row(m.to)) / 2, col(m.from));
        q.key ^= z.ep[q.ep];
    }

    return q;
}

// Add a non-pawn move if it is on board and does not land on a friendly piece
// or king. Kings are never captured; checkmate is represented by no legal reply.
void addMove(MoveList& moves, const Position& p, int from, int to, char promo = 0) {
    if (!inb(row(to), col(to))) return;
    if (sameColor(p.b[to], p.side)) return;
    if (isKing(p.b[to])) return;
    moves.push_back({from, to, promo});
}

// Generate pseudo-legal moves for p.side: piece movement is obeyed, but moves
// that leave p.side in check are filtered later by legalMoves().
void pseudoMoves(const Position& p, MoveList& moves) {
    moves.clear();

    static const int knightD[8][2] = {
        {1,2},{2,1},{2,-1},{1,-2},
        {-1,-2},{-2,-1},{-2,1},{-1,2}
    };
    static const int bishopD[4][2] = {
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    static const int rookD[4][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}
    };

    for (int s = 0; s < H * W; s++) {
        char pc = p.b[s];
        if (!sameColor(pc, p.side)) continue;

        int r = row(s), c = col(s);
        char l = lowerPiece(pc);

        if (l == 'p') {
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
            for (int dc : {-1, 1}) {
                int cc = c + dc;
                if (!inb(rr, cc)) continue;

                int to = idx(rr, cc);
                if (oppColor(p.b[to], p.side) && !isKing(p.b[to])) {
                    bool promote = (p.side == WHITE && rr == 0) ||
                                   (p.side == BLACK && rr == H - 1);

                    if (promote) {
                        for (char pr : {'q', 'r', 'b', 'n'}) {
                            moves.push_back({s, to, pr});
                        }
                    } else {
                        moves.push_back({s, to, 0});
                    }
                }

                if (to == p.ep && isEmpty(p.b[to]) &&
                    oppColor(p.b[idx(r, cc)], p.side) &&
                    lowerPiece(p.b[idx(r, cc)]) == 'p') {
                    moves.push_back({s, to, 0});
                }
            }
        }

        else if (l == 'n') {
            for (auto& d : knightD) {
                int rr = r + d[0], cc = c + d[1];
                if (inb(rr, cc)) addMove(moves, p, s, idx(rr, cc));
            }
        }

        else if (l == 'k') {
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    if (dr == 0 && dc == 0) continue;
                    int rr = r + dr, cc = c + dc;
                    if (inb(rr, cc)) addMove(moves, p, s, idx(rr, cc));
                }
            }
        }

        else {
            if (l == 'b' || l == 'q') {
                for (auto& d : bishopD) {
                    int rr = r + d[0], cc = c + d[1];

                    while (inb(rr, cc)) {
                        int to = idx(rr, cc);

                        if (sameColor(p.b[to], p.side)) break;
                        if (isKing(p.b[to])) break;

                        moves.push_back({s, to, 0});

                        if (oppColor(p.b[to], p.side)) break;

                        rr += d[0];
                        cc += d[1];
                    }
                }
            }

            if (l == 'r' || l == 'q') {
                for (auto& d : rookD) {
                    int rr = r + d[0], cc = c + d[1];

                    while (inb(rr, cc)) {
                        int to = idx(rr, cc);

                        if (sameColor(p.b[to], p.side)) break;
                        if (isKing(p.b[to])) break;

                        moves.push_back({s, to, 0});

                        if (oppColor(p.b[to], p.side)) break;

                        rr += d[0];
                        cc += d[1];
                    }
                }
            }
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

int moveScore(const Position& p, const Move& m, bool forcingMove) {
    char pc = p.b[m.from];
    char piece = lowerPiece(pc);
    int score = 0;

    Position q = makeMove(p, m);
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

void orderMoves(const Position& p, MoveList& moves, bool forcingMove) {
    array<pair<int, Move>, MAX_MOVES> scored;

    for (int i = 0; i < moves.n; i++) {
        scored[i] = {moveScore(p, moves.moves[i], forcingMove), moves.moves[i]};
    }

    stable_sort(scored.begin(), scored.begin() + moves.n,
                [](const auto& a, const auto& b) {
                    return a.first > b.first;
                });

    for (int i = 0; i < moves.n; i++) {
        moves.moves[i] = scored[i].second;
    }
}

struct TTEntry {
    uint64_t key = 0;
    uint8_t moves = 0;
    int8_t result = 0; // 0 = empty, 1 = false, 2 = true.
};

struct TranspositionTable {
    vector<TTEntry> table;
    size_t mask = 0;
    long long hits = 0;
    long long stores = 0;

    void resizeMB(size_t mb) {
        hits = 0;
        stores = 0;

        size_t bytes = mb * 1024ULL * 1024ULL;
        size_t entries = bytes / sizeof(TTEntry);
        size_t pow2 = 1;
        while ((pow2 << 1) <= entries) {
            pow2 <<= 1;
        }

        if (mb == 0 || pow2 == 0) {
            table.clear();
            mask = 0;
            return;
        }

        table.assign(pow2, {});
        mask = pow2 - 1;
    }

    bool enabled() const {
        return !table.empty();
    }

    size_t bytes() const {
        return table.size() * sizeof(TTEntry);
    }

    uint64_t cacheKey(uint64_t positionKey, int moves) const {
        return positionKey ^ (uint64_t(moves) * 0x9e3779b97f4a7c15ULL);
    }

    bool lookup(uint64_t positionKey, int moves, bool& result) {
        if (!enabled()) return false;

        const TTEntry& e = table[cacheKey(positionKey, moves) & mask];
        if (e.result != 0 && e.key == positionKey && e.moves == moves) {
            hits++;
            result = e.result == 2;
            return true;
        }

        return false;
    }

    void store(uint64_t positionKey, int moves, bool result) {
        if (!enabled()) return;

        TTEntry& e = table[cacheKey(positionKey, moves) & mask];
        if (e.result != 0 &&
            (e.key != positionKey || e.moves != moves) &&
            e.moves > moves) {
            return;
        }

        e.key = positionKey;
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
bool forceMateInMoves(Position p, int moves, long long& nodes) {
    nodes++;

    // p.side is the side trying to force mate at this node.
    if (moves <= 0) return false;

    bool cached = false;
    if (tt.lookup(p.key, moves, cached)) {
        return cached;
    }

    MoveList myMoves;
    legalMoves(p, myMoves);
    if (myMoves.empty()) {
        tt.store(p.key, moves, false);
        return false;
    }
    orderMoves(p, myMoves, true);

    for (const Move& m : myMoves) {
        Position q = makeMove(p, m);

        MoveList replies;
        legalMoves(q, replies);
        orderMoves(q, replies, false);

        // Stalemate or no legal replies but not mate is not success.
        if (replies.empty()) {
            if (inCheck(q, q.side)) {
                tt.store(p.key, moves, true);
                return true;
            }
            continue;
        }

        bool worksAgainstEveryReply = true;

        for (const Move& r : replies) {
            Position afterReply = makeMove(q, r);

            if (!forceMateInMoves(afterReply, moves - 1, nodes)) {
                worksAgainstEveryReply = false;
                break;
            }
        }

        if (worksAgainstEveryReply) {
            tt.store(p.key, moves, true);
            return true;
        }
    }

    tt.store(p.key, moves, false);
    return false;
}

// Print whether White can force mate from the initial position in n White moves.
bool whiteCanForceMateIn(const Position& start, int n) {
    Position p = start;
    p.side = WHITE;

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

// Run the mate search up to maxN, defaulting to 5 unless overridden by argv[1].
// argv[2] optionally sets the transposition table size in MB, defaulting to 8.
int main(int argc, char** argv) {
    Position start = initial5x8();

    printBoard(start);

    int maxN = 5;
    if (argc >= 2) {
        maxN = atoi(argv[1]);
    }

    size_t ttMB = 8;
    if (argc >= 3) {
        ttMB = strtoull(argv[2], nullptr, 10);
    }
    tt.resizeMB(ttMB);

    cout << "\nTransposition table: "
         << (tt.bytes() / (1024 * 1024)) << " MB, "
         << tt.table.size() << " entries\n";

    cout << "\nLegal White first moves:\n";
    for (const Move& m : legalMoves(start)) {
        cout << moveName(m) << " ";
    }
    cout << "\n\n";

    cout << "=== White forced mates ===\n";
    for (int n = 1; n <= maxN; n++) {
        whiteCanForceMateIn(start, n);
    }

    cout << "\n=== Black forced mates after White's first move ===\n";
    for (int n = 1; n <= maxN; n++) {
        blackCanForceMateAfterWhiteMove(start, n);
    }

    cout << "\nTT hits=" << tt.hits
         << ", stores=" << tt.stores << "\n";

    return 0;
}
