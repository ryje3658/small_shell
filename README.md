# small_shell
A bash-like shell with a subset of features of those found in bash, including the ability to...
- provide a prompt 
- handle blank lines, comments, and limited variable expansion 
- execute a set of commands written expliticly for the shell (exit, cd, status)
- handle other commands by passing them to appropriate exec functions, spawning new processes
- support input/output redirection 
- ability to run processes in the foreground and background
- implements custom signal handlers
