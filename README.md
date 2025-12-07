# smallsh — Minimal Unix Shell

A custom Unix shell implemented in C that replicates core shell functionality including process execution, job control, input/output redirection, and signal handling.

The shell is designed to behave like a simplified POSIX shell while demonstrating correct low-level use of the Linux process API and signal model.

---

## Capabilities

- Command parsing and execution
- Built-in commands: `exit`, `cd`, `status`
- Foreground and background process management
- Input and output redirection
- Signal handling for `SIGINT` and `SIGTSTP`
- Foreground-only execution mode

---

## Shell Behavior

### Command Interface

**Prompt**
```
:
```

**Syntax**
```
command [arg1 arg2 ...] [< input_file] [> output_file] [&]
```

- Arguments are space-separated.
- `<`, `>`, and `&` must appear as standalone tokens.
- `&` requests background execution.
- Quoting and piping are intentionally not supported.
- Maximum command length: 2048 characters.
- Maximum argument count: 512.

---

### Comments and Blank Lines
- Lines beginning with `#` are treated as comments and ignored.
- Blank lines produce no output.
- The prompt is redisplayed in both cases.

---

## Built-in Commands

### `exit`
- Terminates the shell.
- Sends termination signals to all child processes spawned by the shell before exiting.

### `cd`
- With no arguments, changes to the directory specified by `$HOME`.
- With one argument, changes to the specified relative or absolute directory.

### `status`
- Reports the exit value or terminating signal of the most recent **foreground** command.
- Built-in commands do not update the stored status.
- Before any foreground command has executed, the reported status is exit value `0`.

**Notes**
- Built-in commands always execute in the foreground.
- Built-in commands do not support input or output redirection.

---

## External Command Execution

- The shell forks a child process for each non-built-in command.
- In the child process:
  - Input/output redirection is applied using `dup2` if specified.
  - The command is executed using `execvp`.
- In the parent process:
  - Foreground commands block until completion.
  - Background commands return immediately and report:
    ```
    background pid is <pid>
    ```

If execution fails:
- An error message is printed.
- The foreground exit status is set to `1`.
- The child process exits.

---

## Input and Output Redirection

- Implemented using file descriptor manipulation with `dup2`.
- Input redirection (`<`) opens files read-only.
- Output redirection (`>`) creates or truncates files with write-only access.
- Redirection failures:
  - Print an error message.
  - Set the foreground exit status to `1`.
  - Do not terminate the shell.
- Both input and output redirection may be used simultaneously.

---

## Foreground and Background Processes

### Foreground Execution
- The shell waits for the child process to complete.
- The prompt is redisplayed only after termination.

### Background Execution
- Commands ending in `&` execute in the background unless foreground-only mode is active.
- The shell does not wait for background processes.
- If no redirection is specified:
  - Standard input and output are redirected to `/dev/null`.
- When a background process finishes, the shell reports:
  ```
  background pid <pid> is done: exit value X
  ```
  or
  ```
  background pid <pid> is done: terminated by signal Y
  ```

---

## Signal Handling

### SIGINT (`Ctrl+C`)
- The shell process ignores `SIGINT`.
- Foreground child processes terminate on `SIGINT`.
- Background child processes ignore `SIGINT`.
- When a foreground process is terminated by `SIGINT`, the shell prints:
  ```
  terminated by signal 2
  ```

### SIGTSTP (`Ctrl+Z`)
- Toggles foreground-only mode.
- When enabled:
  - `&` is ignored.
  - The shell prints:
    ```
    Entering foreground-only mode (& is now ignored)
    ```
- When disabled:
  - The shell prints:
    ```
    Exiting foreground-only mode
    ```

---

## Process Cleanup and Exit
- The shell checks for completed background processes using `waitpid` with `WNOHANG`.
- On exit:
  - All active child processes are terminated.
  - The shell then exits cleanly.

---

## Implementation Highlights

- Uses `fork`, `execvp`, and `waitpid` for process control.
- Uses `sigaction` for reliable signal handling.
- Tracks foreground exit status independently of background jobs.
- Ensures correct Unix-like behavior for job control and signals.

---

## Build and Run

Build:
```bash
gcc --std=gnu99 -Wall -o smallsh *.c
```

Run:
```bash
./smallsh
```

---

## Example Session

```
: ls
: ls > junk
: status
exit value 0
: wc < junk > junk2
: sleep 15 &
background pid is 4923
background pid 4923 is done: exit value 0
: ^Z
Entering foreground-only mode (& is now ignored)
: sleep 5 &
: ^Z
Exiting foreground-only mode
: exit
```
