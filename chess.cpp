#include <bits/stdc++.h>
using namespace std;

static constexpr int H = 5;
static constexpr int W = 8;

enum Color { WHITE = 0, BLACK = 1 };

struct Move {
    int from;
    int to;
    char promo; // 0, 'q', 'r', 'b', 'n'
};

struct Position {
    array<char, H * W> b{};
    Color side = WHITE;
    int ep = -1; // En-passant target square, or -1.
};

int idx(int r, int c) { return r * W + c; }
int row(int s) { return s / W; }
int col(int s) { return s % W; }

bool inb(int r, int c) {
    return r >= 0 && r < H && c >= 0 && c < W;
}

bool isWhite(char p) { return p >= 'A' && p <= 'Z'; }
bool isBlack(char p) { return p >= 'a' && p <= 'z'; }
bool isEmpty(char p) { return p == '.'; }
bool isKing(char p) { return p == 'K' || p == 'k'; }

Color other(Color c) {
    return c == WHITE ? BLACK : WHITE;
}

bool sameColor(char p, Color side) {
    if (isEmpty(p)) return false;
    return side == WHITE ? isWhite(p) : isBlack(p);
}

bool oppColor(char p, Color side) {
    if (isEmpty(p)) return false;
    return side == WHITE ? isBlack(p) : isWhite(p);
}

char makePiece(Color side, char lower) {
    return side == WHITE ? toupper(lower) : lower;
}

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
    return p;
}

int findKing(const Position& p, Color side) {
    char k = side == WHITE ? 'K' : 'k';
    for (int i = 0; i < H * W; i++) {
        if (p.b[i] == k) return i;
    }
    return -1;
}

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

    auto ray = [&](int dr, int dc, const string& attackers) -> bool {
        int rr = r + dr, cc = c + dc;
        while (inb(rr, cc)) {
            char x = p.b[idx(rr, cc)];
            if (!isEmpty(x)) return attackers.find(x) != string::npos;
            rr += dr;
            cc += dc;
        }
        return false;
    };

    if (attacker == WHITE) {
        if (ray(1, 0, "RQ") || ray(-1, 0, "RQ") ||
            ray(0, 1, "RQ") || ray(0, -1, "RQ")) return true;

        if (ray(1, 1, "BQ") || ray(1, -1, "BQ") ||
            ray(-1, 1, "BQ") || ray(-1, -1, "BQ")) return true;
    } else {
        if (ray(1, 0, "rq") || ray(-1, 0, "rq") ||
            ray(0, 1, "rq") || ray(0, -1, "rq")) return true;

        if (ray(1, 1, "bq") || ray(1, -1, "bq") ||
            ray(-1, 1, "bq") || ray(-1, -1, "bq")) return true;
    }

    return false;
}

bool inCheck(const Position& p, Color side) {
    int ksq = findKing(p, side);
    if (ksq < 0) return true;
    return squareAttackedBy(p, ksq, other(side));
}

Position makeMove(const Position& p, const Move& m) {
    Position q = p;
    char pc = q.b[m.from];
    bool epCapture = tolower(pc) == 'p' &&
                     m.to == p.ep &&
                     isEmpty(q.b[m.to]) &&
                     col(m.from) != col(m.to);

    q.b[m.from] = '.';
    if (epCapture) {
        q.b[idx(row(m.from), col(m.to))] = '.';
    }
    q.b[m.to] = m.promo ? makePiece(p.side, m.promo) : pc;
    q.side = other(p.side);
    q.ep = -1;

    if (tolower(pc) == 'p' && abs(row(m.to) - row(m.from)) == 2) {
        q.ep = idx((row(m.from) + row(m.to)) / 2, col(m.from));
    }

    return q;
}

void addMove(vector<Move>& moves, const Position& p, int from, int to, char promo = 0) {
    if (!inb(row(to), col(to))) return;
    if (sameColor(p.b[to], p.side)) return;
    if (isKing(p.b[to])) return;
    moves.push_back({from, to, promo});
}

vector<Move> pseudoMoves(const Position& p) {
    vector<Move> moves;

    for (int s = 0; s < H * W; s++) {
        char pc = p.b[s];
        if (!sameColor(pc, p.side)) continue;

        int r = row(s), c = col(s);
        char l = tolower(pc);

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
                    tolower(p.b[idx(r, cc)]) == 'p') {
                    moves.push_back({s, to, 0});
                }
            }
        }

        else if (l == 'n') {
            static const int nd[8][2] = {
                {1,2},{2,1},{2,-1},{1,-2},
                {-1,-2},{-2,-1},{-2,1},{-1,2}
            };

            for (auto& d : nd) {
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
            vector<pair<int,int>> dirs;

            if (l == 'b' || l == 'q') {
                dirs.push_back({1, 1});
                dirs.push_back({1, -1});
                dirs.push_back({-1, 1});
                dirs.push_back({-1, -1});
            }

            if (l == 'r' || l == 'q') {
                dirs.push_back({1, 0});
                dirs.push_back({-1, 0});
                dirs.push_back({0, 1});
                dirs.push_back({0, -1});
            }

            for (auto [dr, dc] : dirs) {
                int rr = r + dr, cc = c + dc;

                while (inb(rr, cc)) {
                    int to = idx(rr, cc);

                    if (sameColor(p.b[to], p.side)) break;

                    if (isKing(p.b[to])) break;

                    moves.push_back({s, to, 0});

                    if (oppColor(p.b[to], p.side)) break;

                    rr += dr;
                    cc += dc;
                }
            }
        }
    }

    return moves;
}

vector<Move> legalMoves(const Position& p) {
    vector<Move> out;

    for (const Move& m : pseudoMoves(p)) {
        Position q = makeMove(p, m);
        if (!inCheck(q, p.side)) out.push_back(m);
    }

    return out;
}

bool isCheckmate(const Position& p) {
    if (!inCheck(p, p.side)) return false;
    return legalMoves(p).empty();
}

string sqName(int s) {
    string out;
    out.push_back(char('a' + col(s)));
    out.push_back(char('1' + (H - 1 - row(s))));
    return out;
}

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
    if (isCheckmate(p)) return false;
    if (moves <= 0) return false;

    vector<Move> myMoves = legalMoves(p);
    if (myMoves.empty()) return false;

    for (const Move& m : myMoves) {
        Position q = makeMove(p, m);

        if (isCheckmate(q)) return true;

        vector<Move> replies = legalMoves(q);

        // Stalemate or no legal replies but not mate is not success.
        if (replies.empty()) continue;

        bool worksAgainstEveryReply = true;

        for (const Move& r : replies) {
            Position afterReply = makeMove(q, r);

            if (!forceMateInMoves(afterReply, moves - 1, nodes)) {
                worksAgainstEveryReply = false;
                break;
            }
        }

        if (worksAgainstEveryReply) return true;
    }

    return false;
}

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

    vector<Move> whiteFirstMoves = legalMoves(p);

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

int main(int argc, char** argv) {
    Position start = initial5x8();

    printBoard(start);

    int maxN = 5;
    if (argc >= 2) {
        maxN = atoi(argv[1]);
    }

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

    return 0;
}
