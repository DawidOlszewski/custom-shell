#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define DEBUG 0
#include "shell.h"

sigset_t sigchld_mask;

static void sigint_handler(int sig) {
  /* No-op handler, we just need break read() call with EINTR. */
  (void)sig;
}

/* Rewrite closed file descriptors to -1,
 * to make sure we don't attempt do close them twice. */
static void MaybeClose(int *fdp) {
  if (*fdp < 0)
    return;
  Close(*fdp);
  *fdp = -1;
}

/* Consume all tokens related to redirection operators.
 * Put opened file descriptors into inputp & output respectively. */
static int do_redir(token_t *token, int ntokens, int *inputp, int *outputp) {
  token_t mode = NULL; /* T_INPUT, T_OUTPUT or NULL */
  int n = 0;           /* number of tokens after redirections are removed */

  for (int i = 0; i < ntokens; i++) {
    /* TODO: Handle tokens and open files as requested. */
#ifdef STUDENT
    #define INPUT_OPT 0
    #define OUTPUT_OPT 1
    const mode_t open_modes[] = {[INPUT_OPT] = O_RDONLY, [OUTPUT_OPT] = O_WRONLY | O_TRUNC | O_CREAT};
    const int open_flags[] = {[INPUT_OPT] = 0, [OUTPUT_OPT] = 0644};
    // from context-free grammar we know that after first occurence of T_INPUT or T_OUTPUT no WORD will occur.
    if(token[i] == T_INPUT || token[i] == T_OUTPUT){
      if(token[i] == T_INPUT){
        MaybeClose(inputp); /* we have to close previous pipe or previous redirection,
          because we will not use it anymore */
        *inputp = Open(token[i+1], open_modes[INPUT_OPT], open_flags[INPUT_OPT]);
      }
      if(token[i] == T_OUTPUT){
        MaybeClose(outputp); // we have to close previous pipe or previous redirection
        *outputp = Open(token[i+1], open_modes[OUTPUT_OPT], open_flags[OUTPUT_OPT]);
      }

      token[i] = T_NULL;
      token[i+1] = T_NULL;
      i++;
    }else{
      n++;
    }
#endif /* !STUDENT */
  }

  token[n] = NULL;
  return n;
}

/* Execute internal command within shell's process or execute external command
 * in a subprocess. External command can be run in the background. */
static int do_job(token_t *token, int ntokens, bool bg) {
  int input = -1, output = -1;
  int exitcode = 0;

  ntokens = do_redir(token, ntokens, &input, &output);

  if (!bg) {
    if ((exitcode = builtin_command(token)) >= 0)
      return exitcode;
  }

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start a subprocess, create a job and monitor it. */
#ifdef STUDENT
  pid_t newpid = Fork();
  if(newpid == 0){ /* new job */
    Setpgid(0,0);
    if(bg == FG){
      setfgpgrp(getpid());
    }
    Signal(SIGINT, SIG_DFL);
    Signal(SIGCHLD, SIG_DFL);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);

   if (input != -1) {
      Dup2(input, STDIN_FILENO);
      MaybeClose(&input);
    }
    if (output != -1) {
      Dup2(output, STDOUT_FILENO);
      MaybeClose(&output);
    }

    external_command(token);

  }else{ /* shell*/
    setpgid(newpid, newpid); /* race condition can occure:
     it is possible that new process will be finished by now.
     We could try to check it by kill(newpid, 0) but another race could
     happen, so still we couln't use Setpgid that would fail with error no matter what.
     WE CANNOT CHANGE GROUP OF ZOMBIE */
    if(bg == FG){
      setfgpgrp(newpid);
    }
    MaybeClose(&input);
    MaybeClose(&output);
    int j = addjob(newpid, bg);
    addproc(j, newpid, token);
    if(bg){
       watchjobs(RUNNING);
    }else{
      exitcode = monitorjob(&mask); 
    }
  }
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

/* Start internal or external command in a subprocess that belongs to pipeline.
 * All subprocesses in pipeline must belong to the same process group. */
static pid_t do_stage(pid_t pgid, sigset_t *mask, int input, int output,
                      token_t *token, int ntokens, bool bg) {
  ntokens = do_redir(token, ntokens, &input, &output);

  if (ntokens == 0)
    app_error("ERROR: Command line is not well formed!");

  /* TODO: Start a subprocess and make sure it's moved to a process group. */
  pid_t pid = Fork();
#ifdef STUDENT
  if (pid) { /* shell */
    if (pgid == 0)
      pgid = pid;
    setpgid(pid, pgid);
    MaybeClose(&input);
    MaybeClose(&output);
  } else { /* new job */
    if (pgid == 0) {
      pgid = getpid();
      if (bg == FG) {
        setfgpgrp(getpid());
      }
    }
    Setpgid(pid, pgid);

    Signal(SIGINT, SIG_DFL);
    Signal(SIGCHLD, SIG_DFL);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);

    if (input != -1) { 
      Dup2(input, STDIN_FILENO);
      MaybeClose(&input);
    }
    if (output != -1) {
      Dup2(output, STDOUT_FILENO);
      MaybeClose(&output);
    }
    external_command(token);
  }
#endif /* !STUDENT */

  return pid;
}

static void mkpipe(int *readp, int *writep) {
  int fds[2];
  Pipe(fds);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  *readp = fds[0];
  *writep = fds[1];
}

/* Pipeline execution creates a multiprocess job. Both internal and external
 * commands are executed in subprocesses. */
static int do_pipeline(token_t *token, int ntokens, bool bg) {
  pid_t pid, pgid = 0;
  int job = -1;
  int exitcode = 0;

  int input = -1, output = -1, next_input = -1;

  mkpipe(&next_input, &output);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start pipeline subprocesses, create a job and monitor it.
   * Remember to close unused pipe ends! */
#ifdef STUDENT

  
  inline token_t* tokens_split(const token_t* tokenstart, token_t d, int* new_len, int* is_last){
    *is_last = 0;
    *new_len = 0;
    token_t* iter = (token_t*)tokenstart;
    while(*iter != T_NULL && *iter != d){
      iter++;
      (*new_len)++;
    }
    if(*iter == d){
      *iter == T_NULL;
    }else{
      *is_last = 1;
    }
    
    return (token_t *)tokenstart;
  }

  int is_last = 0;
  int new_len = 0;
  token_t d = T_PIPE;
  token_t* stagei = tokens_split(token, d, &new_len, &is_last);

  while(true){
    if(is_last){ /* we have created one pipe more than we should 
    and we have to close output before, we will fork(), 
    so we close it now. It will happen just once at the last iteration. */
      MaybeClose(&next_input);
      MaybeClose(&output);
    }
    pid = do_stage(pgid, &mask, input, output, stagei, new_len, bg);
    if (!pgid) { /* unfortuantely we cannot assign group to job before do_stage(), 
    bacause we need proces_id of newly created proces which will be taken from do_stage(). */
      pgid = pid;
      job = addjob(pgid, bg);
    }
    addproc(job, pid, stagei);
    
    if(!is_last){
      input = next_input;
      mkpipe(&next_input, &output);
      stagei = tokens_split(stagei+new_len+1, d, &new_len, &is_last);
    }else{
      break;
    }
  }

  if (bg == FG) {
    exitcode = monitorjob(&mask);
  } else {
    watchjobs(RUNNING);
  }


#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

static bool is_pipeline(token_t *token, int ntokens) {
  for (int i = 0; i < ntokens; i++)
    if (token[i] == T_PIPE)
      return true;
  return false;
}

static void eval(char *cmdline) {
  bool bg = false;
  int ntokens;
  token_t *token = tokenize(cmdline, &ntokens);

  if (ntokens > 0 && token[ntokens - 1] == T_BGJOB) {
    token[--ntokens] = NULL;
    bg = true;
  }

  if (ntokens > 0) {
    if (is_pipeline(token, ntokens)) {
      do_pipeline(token, ntokens, bg);
    } else {
      do_job(token, ntokens, bg);
    }
  }

  free(token);
}

#ifndef READLINE
static char *readline(const char *prompt) {
  static char line[MAXLINE]; /* `readline` is clearly not reentrant! */

  write(STDOUT_FILENO, prompt, strlen(prompt));

  line[0] = '\0';

  ssize_t nread = read(STDIN_FILENO, line, MAXLINE);
  if (nread < 0) {
    if (errno != EINTR)
      unix_error("Read error");
    msg("\n");
  } else if (nread == 0) {
    return NULL; /* EOF */
  } else {
    if (line[nread - 1] == '\n')
      line[nread - 1] = '\0';
  }

  return strdup(line);
}
#endif

int main(int argc, char *argv[]) {
  /* `stdin` should be attached to terminal running in canonical mode */
  if (!isatty(STDIN_FILENO))
    app_error("ERROR: Shell can run only in interactive mode!");

#ifdef READLINE
  rl_initialize();
#endif

  sigemptyset(&sigchld_mask);
  sigaddset(&sigchld_mask, SIGCHLD);

  if (getsid(0) != getpgid(0))
    Setpgid(0, 0);

  initjobs();

  struct sigaction act = {
    .sa_handler = sigint_handler,
    .sa_flags = 0, /* without SA_RESTART read() will return EINTR */
  };
  Sigaction(SIGINT, &act, NULL);

  Signal(SIGTSTP, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);
  Signal(SIGTTOU, SIG_IGN);

  while (true) {
    char *line = readline("# ");

    if (line == NULL)
      break;

    if (strlen(line)) {
#ifdef READLINE
      add_history(line);
#endif
      eval(line);
    }
    free(line);
    watchjobs(FINISHED);
  }

  msg("\n");
  shutdownjobs();

  return 0;
}
