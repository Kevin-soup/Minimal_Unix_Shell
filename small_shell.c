/* Program: Small sh
 * Author: Kevin Lin
 * Date: 11/23/2025
 * Description: This program implements a subset of features of well known OS shells, such as bash. 
 *              The shell also supports non built in commands using execvp(), input/output redirection, 
 *              and a toggle for running commands in either foreground or background.                           
*/

#include <stdio.h>      
#include <stdbool.h>    
#include <stdlib.h>     
#include <string.h>     
#include <unistd.h>     
#include <sys/types.h>  
#include <sys/wait.h>   
#include <fcntl.h>      
#include <signal.h>

// Constants.
#define INPUT_LENGTH 2048
#define MAX_ARGS 512

/* 
* Structure for command line inputs.
*/
struct command_line {
    char* arg_variables[MAX_ARGS + 1];
    int arg_count;
    char* input_file;
    char* output_file;
    bool is_background;
};

// Prototype functions.
struct command_line* parse_input();
void empty_heap_memory(struct command_line* current_command);
int built_in_commands(struct command_line* current_command);
int exec_commands(struct command_line* current_command);
void file_redirection(struct command_line* current_command);
void manage_child_process(struct command_line* current_command, pid_t spawnpid);
void background_tracker();
void handle_signal_tstp(int signo); 

// Global variables. 
int latest_status = 0;
int foreground_only = 0;


/*
* Main program.
* Prompts user for shell commands.
*/
int main() {
    struct command_line* current_command;

    // Ignore SIGINT in the shell.
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Handle SIGTSTP for foreground-only mode.
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_signal_tstp;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    while(true) {
        
        // Checks for completed background child processes.
        background_tracker();

        // Parse command line from shell.
        current_command = parse_input();

        // Handle null from parser.  
        if (current_command == NULL) {
            continue;   
        }

        // Handle built-in commands.
        if (built_in_commands(current_command) == 0) {
            continue;
        }

        // Handle exec functions.
        exec_commands(current_command);
    }

    return EXIT_SUCCESS;
}


/*
* Function: parse_input.
* Parses commands entered into shell. 
*
* Parameter: none. 
* Return: current_command (pointer to the structure).
*/
struct command_line* parse_input() {
    char input_buffer[INPUT_LENGTH]; 

    // Print shell command prompt. 
    printf(": ");
    fflush(stdout);

    // Get user input.
    fgets(input_buffer, INPUT_LENGTH, stdin);

    // Handle blank and comment inputs.
    if (input_buffer[0] == '\n' || input_buffer[0] == '#') {
        return NULL; 
    }

    // Initialize zeroed out command line structure in heap.
    struct command_line* current_command = (struct command_line*) calloc(1,
        sizeof(struct command_line));

    // Tokenize and reads the input.
    char* token = strtok(input_buffer, " \n");

    while (token) {

        // Save inputs to command line structure.
        if (!strcmp(token, "<")) {
        current_command->input_file = strdup(strtok(NULL," \n"));

        } else if (!strcmp(token, ">")) {
        current_command->output_file = strdup(strtok(NULL," \n"));

        } else if (!strcmp(token, "&")) {

            // Check if foreground only mode is toggled.
            if (foreground_only == 0) {
                current_command->is_background = true;
            }

        } else {
        current_command->arg_variables[current_command->arg_count++] = strdup(token);
        }   

        // Add null to the last arguement variable. 
        current_command->arg_variables[current_command->arg_count] = NULL;

        token=strtok(NULL," \n");
    }
    return current_command;
}


/* 
* Function: empty_heap_memory.
* Frees up heap memory used by parser. Pervents memory leaks.
*
* Parameter: current_command (pointer to the structure)
* Return: none.
*/
void empty_heap_memory(struct command_line* current_command) {

    // Empty each command variable string.
    for (int i = 0; i < current_command->arg_count; i++) {
        free(current_command->arg_variables[i]);
    }

    // Empty filename string.
    free(current_command->input_file);
    free(current_command->output_file);

    // Empty command line structure.
    free(current_command);
}


/* 
* Function: built_in_commands.
* Support three built-in commands: exit, cd, and status.
*
* Parameter: current_command (pointer to the structure)
* Return: 0 if command is built-in. -1 otherwise.
*/
int built_in_commands(struct command_line* current_command) {

    // Handles exit command.
    if (strcmp(current_command->arg_variables[0], "exit") == 0) {
        empty_heap_memory(current_command);
        exit(0);
    }

    // Handles cd command.
    if (strcmp(current_command->arg_variables[0], "cd") == 0) {

        // No additional arguement.
        if (current_command->arg_count == 1) {

            // Go to home directory.
            char* home = getenv("HOME");

            if (home != NULL) {
                chdir(home);
            }

        // One additional arguement.
        } else {

            // Go to specified directory.
            int result = chdir(current_command->arg_variables[1]);

            // Handle errors.
            if (result == -1) {
                perror("cd");
            }   
        }

        empty_heap_memory(current_command);
        return 0;
    }

    // Status command.
    if (strcmp(current_command->arg_variables[0], "status") == 0) {

        // Foreground process was terminated by signal.
        if (WIFSIGNALED(latest_status)) {
            printf("terminated by signal %d\n", WTERMSIG(latest_status));

        // Foreground process exited normally.
        } else if (WIFEXITED(latest_status)) {
            printf("exit value %d\n", WEXITSTATUS(latest_status));
        }

        fflush(stdout);
        empty_heap_memory(current_command);
        return 0;

    }
    return -1;
}


/* 
* Function: exec_commands.
* Support non built-in commands using fork and exec.
*
* Parameter: current_command (pointer to the structure)
* Return: 0 if successful. 1 on error.
*/
int exec_commands(struct command_line* current_command) {
    
    // Signal settings for child.
    struct sigaction child_SIGINT = {0};
    struct sigaction child_SIGTSTP = {0};

    // Create child process. 
    pid_t spawnpid = fork();

    switch(spawnpid){

        // Catch fork errors.
        case -1:
            perror("fork");
            return 1;

        // Child process.
        case 0:

            // Background child ignores SIGINT.
            if (current_command->is_background) {
                child_SIGINT.sa_handler = SIG_IGN;
            
            // Foreground child killed by SIGINT.
            } else {
                child_SIGINT.sa_handler = SIG_DFL;
            }

            sigfillset(&child_SIGINT.sa_mask);
            child_SIGINT.sa_flags = 0;
            sigaction(SIGINT, &child_SIGINT, NULL);

            // Children always ignore SIGTSTP.
            child_SIGTSTP.sa_handler = SIG_IGN;
            sigfillset(&child_SIGTSTP.sa_mask);
            child_SIGTSTP.sa_flags = 0;
            sigaction(SIGTSTP, &child_SIGTSTP, NULL);

            // Redirect input/output files.
            file_redirection(current_command);

            // Replace child process with new program.
            execvp(current_command->arg_variables[0], current_command->arg_variables);

            // Handle error if new program not found.
            printf("%s: no such file or directory\n", current_command->arg_variables[0]);
            exit(1);

        // Parent process.
        default:

            // Handle background and foreground of forked child process.
            manage_child_process(current_command, spawnpid);

            empty_heap_memory(current_command);
            return 0;
    }
}


/* 
* Function: file_redirection.
* Redirects standard input and output.
*
* Parameter: current_command (pointer to the structure)
* Return: none.
*/
void file_redirection(struct command_line* current_command) {

    // Input file provided. Standard input goes to file.
    if (current_command->input_file != NULL) {

        // Open selected file.
        int input_descriptor = open(current_command->input_file, O_RDONLY);

        // Handle errors.
        if (input_descriptor == -1) {
            printf("cannot open %s for input\n", current_command->input_file);
            fflush(stdout);
            exit(1);
        }
        
        // Redirect input.
        dup2(input_descriptor, STDIN_FILENO);

        // Close file.
        close(input_descriptor);
    
    // No input file. Standard input goes to /dev/null.
    } else if (current_command->is_background) {

        // Open dev/null file.
        int input_descriptor = open("/dev/null", O_RDONLY);
    
        // Handle errors.
        if (input_descriptor == -1) {
            printf("open error\n");
            fflush(stdout);
            exit(1);
        }

        // Redirect input.
        dup2(input_descriptor, STDIN_FILENO);

        // Close file.
        close(input_descriptor);
    }
    
    // Output file provided. Standard output goes to file.
    if (current_command->output_file != NULL) {

        // Open selected file. Create if missing. Truncate if exists.
        int output_descriptor = open(
            current_command->output_file,
            O_WRONLY | O_CREAT | O_TRUNC,
            0644
        );

        // Handle errors.
        if (output_descriptor == -1) {
            printf("cannot open %s for output\n", current_command->output_file);
            fflush(stdout);
            exit(1); 
        }

        // Redirect output.
        dup2(output_descriptor, STDOUT_FILENO);

        // Close file.
        close(output_descriptor);

    // No output file. Standard output goes to /dev/null.
    } else if (current_command->is_background) {
       
        // Open dev/null file.
        int output_descriptor = open("/dev/null", O_WRONLY);

        // Handle errors.
        if (output_descriptor == -1) {
            printf("open error\n");
            fflush(stdout);
            exit(1);
        }

        // Redirect output.
        dup2(output_descriptor, STDOUT_FILENO);

        // Close file.
        close(output_descriptor);
    }
}


/* 
* Function: manage_child_process.
* Handles foreground and background child processes.
*
* Parameter: current_command (pointer to the structure)
*            spawnpid (process id of the child)
* Return: none.
*/
void manage_child_process(struct command_line* current_command, pid_t spawnpid) {

    int child_status;

    // Background command.
    if (current_command->is_background) {

        // Print background PID when process begins.
        printf("background pid is %d\n", spawnpid);
        fflush(stdout);
        return;
    }

    // Foreground command. Wait for child process to complete.
    waitpid(spawnpid, &child_status, 0);

    // Save status.
    latest_status = child_status;

    // Check if child was terminated by signal.
    if (WIFSIGNALED(child_status)) {

        int signal_number = WTERMSIG(child_status);
        
        // Print PID termination message.
        printf("terminated by signal %d\n", signal_number);
        fflush(stdout);
    }
}


/* 
* Function: background_tracker.
* Checks if background child processes have finished.
*
* Parameter: none.
* Return: none.
*/
void background_tracker() {
    int child_status;
    pid_t completed_pid;

    // Check for completed child processes without block. 
    completed_pid = waitpid(-1, &child_status, WNOHANG);

    while (completed_pid > 0) {

        // Child process exited normally.
        if (WIFEXITED(child_status)) {

            int exit_value = WEXITSTATUS(child_status);
            
            // Print PID completion message.
            printf("background pid %d is done: exit value %d\n",
                   completed_pid, exit_value);
        
        // Child process killed by signal. 
        } else if (WIFSIGNALED(child_status)) {

            int signal_number = WTERMSIG(child_status);

            // Print PID termination message.
            printf("background pid %d is done: terminated by signal %d\n",
                   completed_pid, signal_number);
        }

        fflush(stdout);

        // Check for completed processes.
        completed_pid = waitpid(-1, &child_status, WNOHANG);
    }
} 


/*
* Function: handle_sigtstp.
* Handler for SIGTSTP. Toggle to foreground only, ignoring & operator.
*
* Parameter: signo (integer).
* Return: none.
*/
void handle_signal_tstp(int signo) {

    char* start_message = "\nEntering foreground-only mode (& is now ignored)\n ";
    char* exit_message = "\nExiting foreground-only mode\n ";

    // Enters foreground only mode.
    if (foreground_only == 0) {

        foreground_only = 1;
        write(STDOUT_FILENO, start_message, strlen(start_message));

    // Exits foreground only mode.
    } else {
        foreground_only = 0;
        write(STDOUT_FILENO, exit_message, strlen(exit_message));
    }
}