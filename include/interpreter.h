#ifndef INTERPRETER_H
#define INTERPRETER_H

/* Constants: */
#define CoreTop USER_MEMORY_SIZE  /* Core size  16k from user datatypes*/
#define UserProg 32   /* Core address of front of Basic program */
#define EndUser 34    /* Core address of end of stack/user space */
#define EndProg 36    /* Core address of end of Basic program */
#define GoStkTop 38   /* Core address of Gosub stack top */
#define LinoCore 40   /* Core address of "Current BASIC line number" */
#define ILPCcore 42   /* Core address of "IL Program Counter" */
#define BPcore 44     /* Core address of "Basic Pointer" */
#define SvPtCore 46   /* Core address of "Saved Pointer" */
#define InLine 48     /* Core address of input line */
#define ExpnStk 128   /* Core address of expression stack (empty) */
#define TabHere 191   /* Core address of output line size, for tabs */
#define WachPoint 255 /* Core address of debug watchpoint USR */
#define ColdGo 256    /* Core address of nominal restart USR */
#define WarmGo 259    /* Core address of nominal warm start USR */
#define InchSub 262   /* Core address of nominal char input USR */
#define OutchSub 265  /* Core address of nominal char output USR */
#define BreakSub 268  /* Core address of nominal break test USR */
#define ExitBasic 269 /* Core address of exit BASIC USR */
#define DumpSub 273   /* Core address of debug core dump USR */
#define PeekSub 276   /* Core address of nominal byte peek USR */
#define Peek2Sub 277  /* Core address of nominal 2-byte peek USR */
#define PokeSub 280   /* Core address of nominal byte poke USR */
#define TrLogSub 283  /* Core address of debug trace log USR */
#define BScode 271    /* Core address of backspace code */
#define CanCode 272   /* Core address of line cancel code */
#define ILfront 286   /* Core address of IL code address */
#define BadOp 15      /* illegal op, default IL code */

void ConfigureTinyBasic();
void UserInitTinyBasic(user_context_t *user, char * ILtext);
void RunTinyBasic(user_context_t * user);
void ListIt(user_context_t *user, int from, int to);
void ColdStart(user_context_t *user);
void WarmStart(user_context_t * user);
void Poke2(user_context_t*user, int loc, int valu);
int Peek2(user_context_t *user, int loc);

#endif /* INTERPRETER_H */