// micro-Max chess engine implementation for cyd-chess -- see micromax.h for
// the public interface (Init/ApplyMove/GetBestMove, plus the handful of
// globals this sketch's own undo snapshot has to reach directly).
#include <Arduino.h> // vTaskDelay, uint32_t, rand()
#include "micromax.h"

// H.G. Muller's micro-Max 4.8 (home.hccnet.nl/h.g.muller/umax4_8.c), vendored
// near-verbatim in place of this sketch's earlier plain minimax/PST AI.
// Deliberately kept as its own separate board/search state (mmB[], mmA[]
// hash table, etc.) rather than adapted to run against GameState directly --
// the two board representations (this sketch's 8x8 arrays vs. micro-Max's
// packed 0x88 char board) are too different to safely reconcile inside code
// this dense without real risk of a subtle bug. Instead both boards are kept
// in lockstep: every move -- the player's (already validated by this
// sketch's own generateMoves()) and micro-Max's own -- gets applied to
// *both* representations, and micro-Max's chosen move is cross-checked
// against this sketch's own legal-move list before being trusted (see
// microMaxGetBestMove below), rather than trusting its output blindly.
//
// The interface is deliberately narrow -- just square coordinates in and
// out, never piece types or move-kind flags -- because this sketch's own
// generateMoves() already derives promotion/en-passant/castling from
// from/to squares plus its own tracked game state, so micro-Max's board
// representation and piece encoding never need to be understood at all,
// only its square-numbering: K = file_char - 16*rank_char + 799 (its own
// input-parsing formula, from its reference console UI) reduces to plain
// 16*row+col once row 0 = rank 8 -- which is already this sketch's own
// row/col convention -- so coordinates need no translation beyond that.
//
// Changes from Muller's original source (everything else is byte-for-byte
// as published):
//  - D()'s and main()'s old K&R-style declarations rewritten in standard
//    C++ function-signature form (a plain C++ requirement, not a logic
//    change) -- the body is untouched.
//  - Hash table size (U) shrunk from 1<<24 (16M entries, ~200MB) to
//    MM_U below, to fit ESP32 RAM. A smaller table only means more hash
//    collisions (weaker move-ordering hints), never incorrect play.
//  - Root search node budget (the literal `N<1e6` in the deepening-loop
//    condition) replaced with mmNodeBudget, sized for this hardware's
//    measured node rate rather than Muller's original PC target -- see
//    that variable's own comment for the measurement and reasoning. Made a
//    runtime variable (not a compile-time constant) rather than a macro, so
//    cyd-chess.ino's difficulty selector can set it per game.
//  - char forced explicitly `signed char` throughout this block -- see
//    that declaration's own comment.
//  - mmT[] (the hash translation table, reinterpreted 4 bytes at a time via
//    the K(A,B) macro) gets an explicit 4-byte alignment attribute --
//    Muller's own computed byte-offsets always land on a 4-byte boundary
//    (by construction, not by luck), but nothing in the C++ standard
//    otherwise guarantees mmT[]'s base address itself is 4-byte aligned,
//    which Xtensa's strict-alignment load/store unit requires.
//  - Two lines added right where the original already privately decides
//    "this is the move to commit" (previously a commented-out printf for
//    kibitzing) to capture (x, y) into mmLastX/mmLastY, so the caller can
//    learn which move was just played -- nothing else exposes that.
//  - A periodic vTaskDelay(1) added at the top of every mmD() call (own
//    dedicated counter, not reusing Muller's mmN -- mmN only increments once
//    per already-full-board-scan iteration at a single node, far too coarse
//    a yield granularity for the interrupt watchdog here), so a long search
//    still feeds the watchdog (same reasoning as the old minimax's per-node
//    yield it replaces).
//  - main()'s interactive read/print loop is dropped entirely (folded into
//    microMaxInit()/microMaxApplyMove()/microMaxGetBestMove() below instead)
//    -- an Arduino sketch supplies its own main(), so keeping Muller's would
//    have been a straight link conflict, not just unwanted code.
//  - A background move-rating eval (mmEvalTask and friends, below) added
//    alongside microMaxGetBestMove() -- runs the same root search on a
//    second core to score how good the human's about-to-be-made move was,
//    without touching the vendored search itself. See its own comment.

#define W while // Muller's, #undef'd right after microMaxInit() below to limit its reach
#define MM_U (1 << 12) // hash table size, must stay a power of 2 (see U-1 mask below)

// [cyd-chess addition] Muller's original root search budget was a literal
// `N<1e6` (a million nodes), tuned for 2000s-era PC hardware. Measured on
// this board: ~16,300 nodes/sec, and the search overshoots this threshold
// by roughly 2.8x before the current "iteration in flight" actually stops
// (the budget only gates whether the root *starts* another deepening pass,
// not recursive descents already underway) -- so 1e6 nodes cost ~172
// *seconds* for a cold-hash-table move, unusable for an interactive game.
// 30000 (the Expert-level default -- see cyd-chess.ino's STRENGTH_NODE_BUDGET)
// targets roughly 5s per move (every move -- see the mmN reset in
// microMaxGetBestMove() below for why this budget actually applies fresh
// each move rather than only the first). A runtime variable rather than a
// compile-time constant so the difficulty selector can lower it for a
// weaker/faster-playing AI; microMaxInit() deliberately does not reset it,
// since it's per-game configuration set by the caller, not running state.
int mmNodeBudget = 30000;
struct _ { int K, V; char X, Y, D; } mmA[MM_U];

int mmM = 136, mmS = 128, mmI = 8e3, mmQ, mmO, mmK, mmN, mmR, mmJ, mmZ, mmk = 16;
// (Muller's *p, c[9] globals dropped -- only used by the console getchar()
// input loop this port omits entirely.)
// [cyd-chess: char forced explicitly signed throughout this block --
//  Muller's original targeted x86, where plain `char` defaults signed;
//  Xtensa/ESP32's default is unsigned, which would silently corrupt the
//  negative piece-values/step-vectors this engine's arithmetic depends on]
signed char mmL,
mmW[] = {0, 2, 2, 7, -1, 8, 12, 23},                      /* relative piece values    */
mmOv[] = {-16,-15,-17,0,1,16,0,1,16,15,17,0,14,18,31,33,0, /* step-vector lists */
     7,-1,11,6,8,3,6,                          /* 1st dir. in o[] per piece*/
     6,3,5,7,4,5,3,6},                         /* initial piece setup      */
mmB[129],                                        /* board: half of 16x8+dummy*/
mmT[1035] __attribute__((aligned(4)));           /* hash translation table   */

int mmLastX, mmLastY; // [cyd-chess addition] last committed move's squares

// [cyd-chess addition] forward declaration -- mmD() itself isn't defined until below, but the
// background eval task wrapping it (right below) needs to call it too.
int mmD(int q, int l, int e, int E, int z, int n);

// [cyd-chess addition] Background move-rating eval -- see microMaxStartBackgroundEval()'s
// comment in micromax.h for the feature this supports. Runs mmD()'s exact root search (same
// call microMaxGetBestMove() below makes) on a task pinned to core 0, which this sketch
// otherwise leaves completely idle (no WiFi, no other tasks), so it overlaps the human's think
// time instead of adding a pause after they move. mmD()'s root call commits its chosen line
// onto mmB as a side effect of finding it (see microMaxGetBestMove()'s own comment) -- fine when
// that line IS the move about to be played, not here, so this snapshots mmB/mmJ/mmZ/mmk/mmR/mmQ/
// mmO (the same fields cyd-chess.ino's own undo snapshot saves, for the same reason: it's
// everything that has to travel together to keep micro-Max in lockstep) before searching and
// restores them after, leaving the real game untouched. The hash table (mmA) is deliberately
// *not* saved/restored -- leftover entries from a position that was actually reached only helps
// (a warm cache), the same reasoning already documented on MM_U above for hash collisions never
// being a correctness issue, only a move-ordering one.
//
// Plain volatile flags, no mutex: ESP32's two cores access internal SRAM (where these globals
// live) directly rather than through a per-core cache, so there's no coherency gap to paper
// over -- only one side ever writes a given field at a time (main sets IDLE->RUNNING, the task
// sets RUNNING->READY then deletes itself, main reads/resets READY->IDLE), so plain volatile is
// enough to stop the compiler from reordering or caching a stale value across the boundary.
enum { MM_EVAL_IDLE, MM_EVAL_RUNNING, MM_EVAL_READY };
static volatile int mmEvalState = MM_EVAL_IDLE;
static volatile int mmEvalScore = 0;

static void mmEvalTask(void *) {
  signed char savedB[129];
  memcpy(savedB, mmB, sizeof(mmB));
  int savedJ = mmJ, savedZ = mmZ, savedk = mmk, savedR = mmR, savedQ = mmQ, savedO = mmO;

  mmN = 0;
  mmK = mmI;
  int score = mmD(-mmI, mmI, mmQ, mmO, 1, 3);

  memcpy(mmB, savedB, sizeof(mmB));
  mmJ = savedJ; mmZ = savedZ; mmk = savedk; mmR = savedR; mmQ = savedQ; mmO = savedO;

  mmEvalScore = score;
  mmEvalState = MM_EVAL_READY;
  vTaskDelete(nullptr);
}

void microMaxStartBackgroundEval() {
  if (mmEvalState != MM_EVAL_IDLE) return; // already covers the current position
  mmEvalState = MM_EVAL_RUNNING;
  // Same 64KB cyd-chess.ino gives the main loopTask -- mmD()'s recursion
  // needs it just as much running here.
  xTaskCreatePinnedToCore(mmEvalTask, "mmEval", 64 * 1024, nullptr, 1, nullptr, 0);
}

bool microMaxBackgroundEvalReady() {
  return mmEvalState == MM_EVAL_READY;
}

bool microMaxConsumeBackgroundEval(int &outScore) {
  if (mmEvalState == MM_EVAL_IDLE) return false; // never started
  while (mmEvalState == MM_EVAL_RUNNING) vTaskDelay(1);
  outScore = mmEvalScore;
  mmEvalState = MM_EVAL_IDLE;
  return true;
}

// [cyd-chess addition] Must run before anything touches mm*'s state (mmB, the hash table, etc.)
// -- guards against the exceedingly narrow but real race of a background eval (above) still
// running when the game state it's evaluating is about to change out from under it (e.g. a fast
// MENU -> New Game tap, or Undo, right after the human's own move started a fresh eval).
// microMaxInit()/microMaxApplyMove()/microMaxGetBestMove() below all call this themselves;
// undo.cpp's direct mmB/etc. access (it isn't routed through any of those three) calls the
// public microMaxSyncBackgroundEval() wrapper instead. A no-op in the far more common case of no
// eval in flight.
void microMaxSyncBackgroundEval() {
  while (mmEvalState == MM_EVAL_RUNNING) vTaskDelay(1);
  mmEvalState = MM_EVAL_IDLE; // discard -- whoever's about to mutate state didn't ask for this
}

#define MM_K(A,B) *(int*)(mmT+A+(B&8)+mmS*(B&7))
#define MM_J(A) MM_K(y+A,mmB[y])-MM_K(x+A,u)-MM_K(H+A,t)

int mmD(int q, int l, int e, int E, int z, int n)  /* recursive minimax search, k=moving side, n=depth*/
{                       /* e=score, z=prev.dest; J,Z=hashkeys; return score*/
 int j,r,m,v,d,h,i,F,G,V,P,f=mmJ,g=mmZ,C,s;
 signed char t,p,u,x,y,X,Y,H,B;
 struct _*a=mmA+(mmJ+mmk*E&MM_U-1);                     /* lookup pos. in hash table*/

 // [cyd-chess addition] feed the watchdog on every recursive call, not just
 // once per iterative-deepening step at a node (mmN, below, only increments
 // once per *already-full-board-scan* iteration at a single node -- far too
 // coarse a granularity: one such scan can itself contain thousands of
 // deeper recursive calls, taking far longer between yields than the
 // interrupt watchdog tolerates. Confirmed by hardware testing: with only
 // the mmN-based yield, the very first search call triggered a
 // TG1WDT_SYS_RESET reboot loop before ever returning.
 static uint32_t s_mmYieldCounter = 0;
 if ((++s_mmYieldCounter & 63) == 0) vTaskDelay(1);

 q--;                                          /* adj. window: delay bonus */
 mmk^=24;                                        /* change sides             */
 d=a->D;m=a->V;X=a->X;Y=a->Y;                  /* resume at stored depth   */
 if(a->K-mmZ|z|                                  /* miss: other pos. or empty*/
  !(m<=q|X&8&&m>=l|X&mmS))                       /*   or window incompatible */
  d=Y=0;                                       /* start iter. from scratch */
 X&=~mmM;                                        /* start at best-move hint  */

 W(d++<n||d<3||                                /* iterative deepening loop */
   z&mmK==mmI&&(mmN<mmNodeBudget&d<98||             /* root: deepen upto time   */
   (mmK=X,mmL=Y&~mmM,d=3)))                              /* time's up: go do best    */
 {x=B=X;                                       /* start scan at prev. best */
  h=Y&mmS;                                       /* request try noncastl. 1st*/
  P=d<3?mmI:mmD(-l,1-l,-e,mmS,0,d-3);                /* Search null move         */
  m=-P<l|mmR>35?d>2?-mmI:e:-P;                     /* Prune or stand-pat       */
  mmN++;                                         /* node count (for timing) */
  do{u=mmB[x];                                   /*  scan board looking for   */
   if(u&mmk)                                     /*  own piece (inefficient!)*/
   {r=p=u&7;                                   /* p = piece type (set r>0) */
    j=mmOv[p+16];                              /* first step vector f.piece*/
    W(r=p>2&r<0?-r:-mmOv[++j])                    /* loop over directions o[] */
    {A:                                        /* resume normal after best */
     y=x;F=G=mmS;                                /* (x,y)=move, (F,G)=castl.R*/
     do{                                       /* y traverses ray, or:     */
      H=y=h?Y^h:y+r;                           /* sneak in prev. best move */
      if(y&mmM)break;                            /* board edge hit           */
      m=E-mmS&mmB[E]&&y-E<2&E-y<2?mmI:m;             /* bad castling             */
      if(p<3&y==E)H^=16;                       /* shift capt.sqr. H if e.p.*/
      t=mmB[H];if(t&mmk|p<3&!(y-x&7)-!t)break;     /* capt. own, bad pawn mode */
      i=37*mmW[t&7]+(t&192);                     /* value of capt. piece t   */
      m=i<0?mmI:m;                               /* K capture                */
      if(m>=l&d>1)goto C;                      /* abort on fail high       */

      v=d-1?e:i-p;                             /* MVV/LVA scoring          */
      if(d-!t>1)                               /* remaining depth          */
      {v=p<6?mmB[x+8]-mmB[y+8]:0;                  /* center positional pts.   */
       mmB[G]=mmB[H]=mmB[x]=0;mmB[y]=u|32;             /* do move, set non-virgin  */
       if(!(G&mmM))mmB[F]=mmk+6,v+=50;               /* castling: put R & score  */
       v-=p-4|mmR>29?0:20;                       /* penalize mid-game K move */
       if(p<3)                                 /* pawns:                   */
       {v-=9*((x-2&mmM||mmB[x-2]-u)+               /* structure, undefended    */
              (x+2&mmM||mmB[x+2]-u)-1              /*        squares plus bias */
             +(mmB[x^16]==mmk+36))                 /* kling to non-virgin King */
             -(mmR>>2);                          /* end-game Pawn-push bonus */
        V=y+r+1&mmS?647-p:2*(u&y+16&32);         /* promotion or 6/7th bonus */
        mmB[y]+=V;i+=V;                          /* change piece, add score  */
       }
       v+=e+i;V=m>q?m:q;                       /* new eval and alpha       */
       mmJ+=MM_J(0);mmZ+=MM_J(8)+G-mmS;                /* update hash key          */
       C=d-1-(d>5&p>2&!t&!h);
       C=mmR>29|d<3|P-mmI?C:d;                     /* extend 1 ply if in check */
       do
        s=C>2|v>V?-mmD(-l,-V,-v,                 /* recursive eval. of reply */
                              F,0,C):v;        /* or fail low if futile    */
       W(s>q&++C<d);v=s;
       if(z&&mmK-mmI&&v+mmI&&x==mmK&y==mmL)              /* move pending & in root:  */
       {mmQ=-e-i;mmO=F;                            /*   exit if legal & found  */
        a->D=99;a->V=0;                        /* lock game in hash as draw*/
        mmLastX=x;mmLastY=y;                    /* [cyd-chess addition] capture committed move */
        mmR+=i>>7;return l;                      /* captured non-P material  */
       }
       mmJ=f;mmZ=g;                                /* restore hash key         */
       mmB[G]=mmk+6;mmB[F]=mmB[y]=0;mmB[x]=u;mmB[H]=t;     /* undo move,G can be dummy */
      }
      if(v>m)                                  /* new best, update max,best*/
       m=v,X=x,Y=y|mmS&F;                        /* mark double move with S  */
      if(h){h=0;goto A;}                       /* redo after doing old best*/
      if(x+r-y|u&32|                           /* not 1st step,moved before*/
         p>2&(p-4|j-7||                        /* no P & no lateral K move,*/
         mmB[G=x+3^r>>1&7]-mmk-6                   /* no virgin R in corner G, */
         ||mmB[G^1]|mmB[G^2])                      /* no 2 empty sq. next to R */
        )t+=p<5;                               /* fake capt. for nonsliding*/
      else F=y;                                /* enable e.p.              */
     }W(!t);                                   /* if not capt. continue ray*/
  }}}W((x=x+9&~mmM)-B);                          /* next sqr. of board, wrap */
C:if(m>mmI-mmM|m<mmM-mmI)d=98;                       /* mate holds to any depth  */
  m=m+mmI|P==mmI?m:0;                          /* best loses K: (stale)mate*/
  if(a->D<99)                                  /* protect game history     */
   a->K=mmZ,a->V=m,a->D=d,                       /* always store in hash tab */
   a->X=X|8*(m>q)|mmS*(m<l),a->Y=Y;              /* move, type (bound/exact),*/
 }                                             /*    encoded in X S,8 bits */
 mmk^=24;                                        /* change sides back        */
 return m+=m<e;                                /* delayed-loss bonus       */
}

void microMaxInit() {
 microMaxSyncBackgroundEval();           // [cyd-chess addition] see its own comment above
 memset(mmA, 0, sizeof(mmA));      // [cyd-chess addition] clear hash between games
 // [cyd-chess addition] Muller's own setup loop below only ever explicitly
 // writes the back ranks/pawn ranks (rows 0,1,6,7) -- it relies on the rest
 // of the board (rows 2-5, where pieces actually move to and from during
 // play) already being zeroed process-wide memory, true only the *first*
 // time this runs in a process. Every later call in the same process (this
 // sketch calls microMaxInit() again on every New Game/MENU/restart, never
 // rebooting) would otherwise leave whatever pieces the *previous* game
 // moved into the middle of the board still sitting there, corrupting the
 // very next game's move search. Found via a multi-game-per-process
 // off-device test; confirmed absent when each game ran in its own process.
 memset(mmB, 0, sizeof(mmB));
 mmK=8;W(mmK--)
 {mmB[mmK]=(mmB[mmK+112]=mmOv[mmK+24]+8)+8;mmB[mmK+16]=18;mmB[mmK+96]=9;  /* initial board setup*/
  mmL=8;W(mmL--)mmB[16*mmL+mmK+8]=(mmK-4)*(mmK-4)+(mmL-3.5)*(mmL-3.5); /* center-pts table   */
 }                                                   /*(in unused half b[])*/
 mmN=1035;W(mmN-->mmM)mmT[mmN]=rand()>>9;
 mmJ=mmZ=mmQ=mmO=mmN=mmR=0;        // [cyd-chess addition] reset per-game running state
 mmk=16;                           // [cyd-chess addition] White moves first
}
#undef W // limit this generic-named macro to just the vendored block above

void microMaxApplyMove(int fromRow, int fromCol, int toRow, int toCol) {
 microMaxSyncBackgroundEval(); // [cyd-chess addition] see its own comment above
 mmK = fromRow * 16 + fromCol;
 mmL = toRow * 16 + toCol;
 mmD(-mmI, mmI, mmQ, mmO, 1, 3);
}

// Runs micro-Max's own search (no pending move given, so mmK stays at the
// sentinel mmI until the search itself sets it -- see the long comment
// above), then reports the from/to squares of whatever it just committed
// to its own board. Caller is expected to cross-check this against its own
// legal-move list before trusting it (this sketch's loop() does).
void microMaxGetBestMove(int &fromRow, int &fromCol, int &toRow, int &toCol, int *outScore) {
 microMaxSyncBackgroundEval(); // [cyd-chess addition] see its own comment above
 // [cyd-chess fix] mmN (the root deepening loop's node counter, checked
 // against MM_NODE_BUDGET) is never reset by Muller's own code -- it only
 // resets once per NEW GAME, in microMaxInit(). Left alone, the first move
 // of a game exhausts the whole budget and every later move's root call
 // starts already over budget, so its "keep deepening" check fails
 // immediately and it silently falls back to the bare depth-3 search with
 // no time-based extension at all -- for the rest of that game. (This is
 // what actually made moves 2+ fast in the ~4s/cold-move measurement
 // documented above; earlier attribution of that to hash-table warming was
 // wrong.) Reset here so every move gets the real search budget.
 mmN = 0;
 mmK = mmI;
 int score = mmD(-mmI, mmI, mmQ, mmO, 1, 3);
 if (outScore) *outScore = score;
 fromRow = mmLastX / 16; fromCol = mmLastX & 7;
 toRow = mmLastY / 16;   toCol = mmLastY & 7;   // &7 also drops mmLastY's S/double-move flag bit
}

