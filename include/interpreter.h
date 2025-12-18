#ifndef INTERPRETER_H
#define INTERPRETER_H

void ConfigureTinyBasic();
void UserInitTinyBasic(user_context_t *user, char * ILtext);
void RunTinyBasic(user_context_t * user);
void ListIt(int from, int to);
void ColdStart(user_context_t *user);
void WarmStart(user_context_t * user);

#endif /* INTERPRETER_H */