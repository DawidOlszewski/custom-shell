# Custom Unix Shell
*made for operating system classes*

## Overview

This project is a custom Unix-like shell written in C. It supports running built-in and external commands, job control for foreground and background tasks, input/output redirection, and signal handling for process management. The shell mimics the behavior of standard Unix shells, providing essential functionality with a focus on process control and terminal management.

## Features

- **Command Execution**: Runs both internal and external commands.
- **Job Control**: Supports foreground (`fg`) and background (`bg`) job management.
- **Pipelines**: Allows chaining commands using pipes (`|`).
- **Input/Output Redirection**: Supports `<`, `>`, and `>>` operators for file I/O.
- **Signal Handling**:
  - `SIGINT` (Ctrl+C): Terminates foreground tasks.
  - `SIGCHLD`: Monitors background jobs and cleans up completed processes.
- **Built-in Commands**:
  - `cd [directory]`: Change the current working directory.
  - `fg [n]`: Move a background job to the foreground.
  - `bg [n]`: Resume a stopped background job.
  - `kill %n`: Terminate a specific job.
  - `jobs`: Display active background jobs.

## Usage

### Compilation

To compile the shell, use the provided `Makefile`:

```bash
make
```

### Running the Shell

After compiling, run the shell with:

```bash
./shell
```

### Example Commands

```bash
ls -l                      # List files in long format
cd /home                   # Change directory to /home
sleep 10 &                 # Run sleep command in the background
jobs                        # List background jobs
fg 1                        # Bring job 1 to the foreground
kill %1                     # Kill job 1
```

### Piping and Redirection

```bash
ls -l | grep ".c"           # Pipe output of ls to grep
cat input.txt > output.txt  # Redirect output to a file
wc -l < input.txt           # Redirect input from a file
```

## Code Structure

- `shell.c`: Core shell loop and command execution.
- `jobs.c`: Job control and process management.
- `lexer.c`: Tokenizes input commands.
- `command.c`: Manages execution of external and built-in commands.
- `Makefile`: Automates compilation and cleanup.

## How It Works

1. **Input Parsing**: Reads and tokenizes user input.
2. **Command Execution**: Determines if the command is built-in or external.
3. **Job Control**: Manages background and foreground processes.
4. **Signal Handling**: Captures and handles Unix signals for proper job management.

## Requirements

- GCC Compiler (C99 standard)
- Linux or Unix-based OS

## Testing

Run the following to test for memory leaks and ensure proper execution:

```bash
make test
```