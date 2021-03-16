#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h> 
#include <limits.h>
#include <signal.h>

pid_t pid; // Global variable to store process ID of smallsh itself
int statusExit; // Global variable to store last exit status, returned when user uses built in status command
int statusSignal; // Global variable to store last signal, returned when user uses built in status command
int exitTrue; // Boolean to say that we are looking at exit value as the variable returned by built in status
int foregroundOnlyMode; // Boolean to indicate whether the shell is in foreground only mode
int numberOfProcesses; // Keeps track of the number of processes - 0 indexed
pid_t processes[100]; // Array to store pids of running processes

/* Struct for command information */
struct userCommand
{
    int toBackground; // Used to determine if command is supposed to be run in background
    char* inputFile;
    char* outputFile;
    char* argument[512]; // maximum number of arguments is 512 of undetermined length
    char* command; // flexible w/ no character limit
};

/* Struct for background process information */
struct bgProcessInfo
{
    pid_t childID;
    int childExitMethod;
};

/* Initialize empty structs for signal handling */
struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0}, ignore_action = {0}, default_action = {0};

// Handler for SIGTSTP, sets global variable foregroundOnly mode accordingly
void handle_SIGTSTP(int signo){
    if (foregroundOnlyMode == 0){
        // Switch to foreground only mode
        foregroundOnlyMode = 1;
        waitpid(processes[numberOfProcesses-1], -5, 0); // waits for current foreground process to terminate
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 48);
        fflush(stdout);
    }
    else {
        // Switch out of foreground only mode
        foregroundOnlyMode = 0;
        waitpid(processes[numberOfProcesses-1], -5, 0); // waits for current foreground process to terminate
        char* message = "Exiting foreground-only mode\n";
	    write(STDOUT_FILENO, message, 28);
        fflush(stdout);
    }
}

/* Receives a poitner to a string and replaces any instances of '$$' with the smallsh process id (done in place) */
/* Referenced stackoverflow for this function */
void replaceSubstring(char *target, const char *needle, const char *replacement)
{
    char buffer[1024] = { 0 };
    char *insert_point = &buffer[0];
    const char *tmp = target;
    size_t needle_len = strlen(needle);
    size_t repl_len = strlen(replacement);

    while (1) {
        const char *p = strstr(tmp, needle);

        if (p == NULL) {
            strcpy(insert_point, tmp);
            break;
        }

        memcpy(insert_point, tmp, p - tmp);
        insert_point += p - tmp;
        memcpy(insert_point, replacement, repl_len);
        insert_point += repl_len;

        tmp = p + needle_len;
    }
    strcpy(target, buffer);
}

/* Built in command to change directory, changes to HOME directory if no argument provided */
void builtinCD(struct userCommand* currentCommand){
    // Define home directory and current working directory
    char* home = getenv("HOME");
    char* cwd;
    char buff[PATH_MAX + 1];
    cwd = getcwd(buff, PATH_MAX + 1);
    
    // Given no arguments, change directory to HOME
    if (currentCommand->argument[0] == NULL){
        chdir(home);
    }
    // Given an argument, change directory according to argument (relative or absolute paths handled)
    else {
        char givenPath[PATH_MAX + 1];
        strcpy(givenPath, currentCommand->argument[0]);

        // Go to home
        if (strcmp(givenPath, "~") == 0){
            chdir(home);
        }
        // Go to path specified
        else {
            chdir(givenPath);  //cwd = getcwd(buff, PATH_MAX + 1);    printf("WHERE WE ARE: %s", cwd); (for debugging)
        }
    }
}

/* Built in command to print out exit status or terminating signal of the last foreground process ran by shell */
void builtinStatus(){
    // If exitTrue Boolean, print out the last exit status, otherwise print out the last terminating signal
    if (exitTrue){
        printf("exit value %d\n", statusExit);
        fflush(stdout);
    }
    else{
        printf("terminated by signal %d\n", statusSignal);
        fflush(stdout);
    }
}

/* Built in command to exit shell, kills any other processes or jobs started by the shell before terminating itself */
void builtinExit(){
    // Iterate over list of stored PIDS and kill all processes
    int i;
    for (i = 0; i < numberOfProcesses; i++){
        kill(processes[i], SIGKILL);
        // printf("KILLED PROCESS: %d\n", processes[i]); // FOR DEBUGGING PURPOSES
    }
    exit(0); // Terminates calling process (smallsh)
}

/* Handles redirecting input/output for commands which require it, then passes along to handleExecCommand function */
/* Referenced Exploration: Processes and I/O in course modules */
void redirectIO(struct userCommand* currentCommand){

    // Handle input redirection
    if (currentCommand->inputFile != NULL){
        // Open source file
        int sourceFD = open(currentCommand->inputFile, O_RDONLY);
        // If file can't be opened, print error and set exit status to 1
        if (sourceFD == -1) { 
            printf("cannot open %s file for input\n", currentCommand->inputFile);
            fflush(stdout);
            exit(1); 
        }

        // Redirect stdin to source file
        int result = dup2(sourceFD, 0);
        if (result == -1) { 
            perror("source dup2()"); 
            exit(2); 
        }
    }

    // Handle output redirection
    if (currentCommand->outputFile != NULL){
        // Open target file
        int targetFD = open(currentCommand->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        // If file can't be opened, print error and set exit status to 1
        if (targetFD == -1) { 
            printf("cannot open %s file for output\n", currentCommand->outputFile);
            fflush(stdout);
            exit(1); 
        }
        
        // Redirect stdout to target file
        int result = dup2(targetFD, 1);
        if (result == -1) { 
            perror("target dup2()"); 
            exit(2); 
        }
    }
}

/* Handles redirecting input to dev/null/ for background commands */
/* Referenced Exploration: Processes and I/O in course modules */
void redirectItoDEV(struct userCommand* currentCommand){

    // Set input redirection to dev/null/
    int sourceFD = open("/dev/null", O_RDONLY);

    // Redirect stdin to source file
    int result = dup2(sourceFD, 0);
}

/* Handles redirecting output to dev/null/ for background commands */
/* Referenced Exploration: Processes and I/O in course modules */
void redirectOtoDEV(struct userCommand* currentCommand){

    // Set output redirection to dev/null/
    int targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    
    // Redirect stdout to target file
    int result = dup2(targetFD, 1);
}

/* Handles commands that are not builtins and running in the foreground */
void handleExecCommand(struct userCommand* currentCommand){
    // Creates a NULL terminated array of the arguments in the currentCommand struct to be used with execvp()
    char* argv[513]; // To hold a copy of the arguments
    argv[0] = currentCommand->command;
    int i;
    for(i = 0; i < 512; i++){
        if (currentCommand->argument[i] != NULL) {
            argv[i+1] = currentCommand->argument[i];
        }
        else {
            argv[i+1] = NULL;
            break;
        }
    }

    // Creates child process to handle the command
    pid_t spawnpid = -5;
    int childExitMethod = -5;

    spawnpid = fork(); // THIS IS WHERE THE NEW PROCESS SPAWNS - child starts here and continues
    switch (spawnpid)
    {
        // Something went wrong, fork returns -1 to parent process, sets global variable errno, no child process created
        case -1:
            statusExit = 1;
            exitTrue = 1;
            break;
        // In child process fork() returns 0
        case 0:
            // Set SIGNIT action back to default (unlike the shell and background process which ignore it)
            default_action.sa_handler = SIG_DFL;
            sigaction(SIGINT, &default_action, NULL);

            // Set SIGTSTP to be ignored by the child process
            ignore_action.sa_handler = SIG_IGN;
            sigaction(SIGTSTP, &ignore_action, NULL);

            // Check if process requires IO redirection- redirect IO if so
            if (currentCommand->inputFile != NULL || currentCommand->outputFile != NULL){
                redirectIO(currentCommand);
                }
            // If command could not be executed, print message to user and set value of status command to 1, otherwise execute
            if (execvp(argv[0], argv) < 0){
                printf("Command could not be executed...\n");
                fflush(stdout);
                statusExit = 1;
                exitTrue = 1;
            }
            break;
        // In parent process fork() returns process id of the child process that was just created
        default:
            processes[numberOfProcesses] = spawnpid; // Put pid of child process into array of processes, then increment for next pid
            numberOfProcesses++;
            waitpid(spawnpid, &childExitMethod, 0); // waits for the child process to terminate

            // If terminated normally, set global status to the exit status
            if (WIFEXITED(childExitMethod)){
                int exitStatus = WEXITSTATUS(childExitMethod);
                statusExit = exitStatus;
                exitTrue = 1;
            }
            // If terminated by signal, set global status to the terminating signal
            else if (WIFSIGNALED(childExitMethod)) {
                int termSignal = WTERMSIG(childExitMethod);
                statusSignal = termSignal;
                exitTrue = 0;
            }
            break;
    }
}

/* Handles commands that are not builtins and running in the background, returns a pointer to struct of info for that background process including id and exitmethod */
struct bgProcessInfo *handleBackgroundCommand(struct userCommand* currentCommand){
    // Creates a NULL terminated array of the arguments in the currentCommand struct to be used with execvp()
    char* argv[513]; // To hold a copy of the arguments
    argv[0] = currentCommand->command;
    int i;
    for(i = 0; i < 512; i++){
        if (currentCommand->argument[i] != NULL) {
            argv[i+1] = currentCommand->argument[i];
        }
        else {
            argv[i+1] = NULL;
            break;
        }
    }

    // Creates child process to handle the command
    pid_t spawnpid = -5;
    int childExitMethod = -5;

    spawnpid = fork(); // THIS IS WHERE THE NEW PROCESS SPAWNS - child starts here and continues
    switch (spawnpid)
    {
        // Something went wrong, fork returns -1 to parent process, sets global variable errno, no child process created
        case -1:
            statusExit = 1;
            exitTrue = 1;
            break;
        // In child process fork() returns 0
        case 0:
            // Check if process requires IO redirection- redirect IO if so
            if (currentCommand->inputFile != NULL || currentCommand->outputFile != NULL){
                redirectIO(currentCommand);
            }
            // If user doesn't redirect standard input, then standard input redirected to /dev/null
            if (currentCommand->inputFile == NULL){
                redirectItoDEV(currentCommand);
            }
            // If user doesn't redirect standard output, then standard input redirected to /dev/null
            if (currentCommand->outputFile == NULL){
                redirectOtoDEV(currentCommand);
            }

            // Set SIG_INT to be ignored by the background process
            ignore_action.sa_handler = SIG_IGN;
            sigaction(SIGINT, &ignore_action, NULL);

            // Set SIGTSTP to be ignored by the background process
            ignore_action.sa_handler = SIG_IGN;
            sigaction(SIGTSTP, &ignore_action, NULL);

            // If command could not be executed, print message to user and set value of status command to 1, otherwise execute 
            if (execvp(argv[0], argv) < 0){
                printf("Command could not be executed...\n");
                fflush(stdout);
                statusExit = 1;
                exitTrue = 1;
            }
            break;
        // In parent process fork() returns process id of the child process that was just created
        default:
            processes[numberOfProcesses] = spawnpid; // Put pid of child process into array of processes, then increment for next pid
            numberOfProcesses++;

            // Create struct with child process info (ID and exitMethod) for use in main shell function, return that struct
            struct bgProcessInfo *info  = malloc(sizeof(struct bgProcessInfo));
            info->childID = spawnpid;
            info->childExitMethod = childExitMethod;
            return info;
    }
}

/* Parse the command given by user into the command struct, returns pointer to that struct*/
struct userCommand *createCommand(char *userInput)
{
    userInput[strlen(userInput)-1] = '\0'; // Remove newline character from userInput
    struct userCommand *currCommand = malloc(sizeof(struct userCommand));

    currCommand->toBackground = 0; // Set toBackground value to False as default behavior

    char expansionBuffer[10]; // For use with strreplace function
    snprintf(expansionBuffer, 10, "%d", pid);
    char tmp_token[200]; // To store value in token and copy it over after varaible expansion

    // For use with strtok_r
    char *saveptr;
    char *end;

    // Get command data and put it into struct attribute, replacing $$ with the PID
    char *token = strtok_r(userInput, " ", &saveptr);
    strcpy(tmp_token, token);
    replaceSubstring(tmp_token, "$$", expansionBuffer);
    currCommand->command = calloc(strlen(tmp_token) + 1, sizeof(char));
    strcpy(currCommand->command, tmp_token);
    memset(tmp_token, '\0', sizeof(tmp_token));

    // Get arguments data and put them into struct attribute
    int arg_count = 0;
    while (1) {
        token = strtok_r(NULL, " ", &saveptr);
        if (token == NULL){
            break;
        }
        else if (token[0] == '<' || token[0] == '>'){ // Check for input/output redirects
            if (token[0] == '<'){ // Input redirection- get filename for input and put it into struct attribute
                token = strtok_r(NULL, " ", &saveptr);
                currCommand->inputFile = calloc(strlen(token) + 1, sizeof(char));
                strcpy(currCommand->inputFile, token);
            }
            else if (token[0] == '>'){ // Output redirection- get filename for output and put it into struct attribute
                token = strtok_r(NULL, " ", &saveptr);
                currCommand->outputFile = calloc(strlen(token) + 1, sizeof(char));
                strcpy(currCommand->outputFile, token);
            }
        }
        else if (token[0] == '&'){
            // Check to make sure there are no more arguments given by calling for next token
            token = strtok_r(NULL, " ", &saveptr);
            if (token == NULL){ // If there are no more arguments given then this means last argument so command is to be executed in background
                currCommand->toBackground = 1;
            }
        }
        else {
            strcpy(tmp_token, token);
            replaceSubstring(tmp_token, "$$", expansionBuffer); // Replace $$ with PID
            currCommand->argument[arg_count] = calloc(strlen(tmp_token) + 1, sizeof(char));
            strcpy(currCommand->argument[arg_count], tmp_token);
            memset(tmp_token, '\0', sizeof(tmp_token));
        }
        arg_count++;
    }
    // Print statement for debugging- prints all input given to as a command
    // printf("COMMAND: %s, ARGS: %s, INPUT: %s, OUTPUT %s, BG: %d\n", currCommand->command, currCommand->argument[0], currCommand->inputFile, currCommand->outputFile, currCommand->toBackground);
    // fflush(stdout);
    return currCommand;
}


int main(){
    int backgroundProcessRunning = 0; // Boolean variable, default no background process running
    foregroundOnlyMode = 0; // Boolean variable, default is not in foreground only mode
    numberOfProcesses = 0; // Set number of processes to zero before any commands run
    exitTrue = 1; // Set exit status boolean to False before any commands run
    statusExit = 0; // Set global variable to 0 before any commands run
    pid = getpid(); // Set global variable pid to process ID of smallsh
    struct bgProcessInfo *bgCommandInfo;
    char commandInput[2050]; // Maximum length of command is 2048 characters and it receives new line char as well
    
    // Set SIG_INT to be ignored by the shell
    ignore_action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &ignore_action, NULL);

    // Set SIG_TSTP to call handler defined above, and set the shell to 'foreground only' state where it ignores '&' in commands
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Main loop for shell prompt, continues prompting until user enters 'exit'
    while (1 == 1){

        // Check if background process has terminated- print message with the ended processes id and exit status if so
        int childExitMethod = -5;
        if (backgroundProcessRunning == 1){
            if(waitpid(bgCommandInfo->childID, &childExitMethod, WNOHANG) != 0) {

            // If background process terminated normally, print out that status to terminal
            if (WIFEXITED(childExitMethod)){
                int exitStatus = WEXITSTATUS(childExitMethod);
                printf("background pid %d is done: exit value %d\n", bgCommandInfo->childID, exitStatus);
                fflush(stdout);
            }
            // If background process terminated by signal, print that status to terminal
            else if (WIFSIGNALED(childExitMethod)) {
                int termSignal = WTERMSIG(childExitMethod);
                printf("background pid %d is done: terminated by signal %d\n", bgCommandInfo->childID, termSignal);
                fflush(stdout);
            }
                // Reset backgroundProcessRunning boolean variable to 0 as process has terminated
                backgroundProcessRunning = 0;
            }
        }

        // After checking for background processes, display shell prompt
        printf(": ");
        fflush(stdout);
        fgets(commandInput, 2050, stdin);
        if (commandInput[0] == '\n') {} // If given no input, do nothing
        else if (strncmp(commandInput, "#" , 1) == 0){} // If line begins with #, it is a comment line
        else {
            struct userCommand *currentCommand = createCommand(commandInput);    // User gives input, pass it to createCommand to parse the command and arguments, create pointer to struct
            // Handles built in command "cd"
            if (strcmp(currentCommand->command, "cd") == 0){
                builtinCD(currentCommand);
            }
            // Handles built in command "status"
            else if (strcmp(currentCommand->command, "status") == 0){
                builtinStatus();
            }
            // Handles built in command "exit"
            else if (strcmp(currentCommand->command, "exit") == 0){ 
                builtinExit();
            }
            // Handles commands through exec
            else{
                // If foreground only mode, only run commands in foreground
                if (foregroundOnlyMode == 1){
                    handleExecCommand(currentCommand);
                }
                // If not in foreground only mode, check if command is to be run in the background
                else{
                    // Checks if command is supposed to be run in background, passes to appropriate function to execute
                    if (currentCommand->toBackground == 0){
                        handleExecCommand(currentCommand);
                    }
                    else if (currentCommand->toBackground == 1){
                        bgCommandInfo = handleBackgroundCommand(currentCommand);
                        printf("background pid is %d\n", bgCommandInfo->childID); // Print message to user with background pid
                        fflush(stdout);
                        backgroundProcessRunning = 1; // Set backgroundProcessRunning to true to trigger conditional before next shell prompt
                    }
                }
            }
        }
        memset(commandInput, '\0', sizeof(commandInput)); // Reset commandInput back to nothing
    }
}