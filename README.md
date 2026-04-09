# MiniUnixShell

A lightweight UNIX shell implemented in C that supports basic command execution, piping, redirection, history, and signal handling.

---

## Features

* Custom shell prompt (`sh>`)
* Execute system commands using `fork()` and `execvp()`
* Input (`<`) and output (`>`, `>>`) redirection
* Command piping (`|`) with multiple stages
* Multiple commands using `;`
* Conditional execution using `&&`
* Command history with arrow key navigation
* Built-in commands: `cd`, `exit`, `history`
* Background execution (`&`)
* Signal handling (`Ctrl+C` does not terminate the shell)

---

## Technologies Used

* C Programming Language
* UNIX system calls and POSIX libraries

---

## Compilation

```bash
gcc shell.c -o shell
```

---

## Run

```bash
./shell
```

---

## Example Usage

```bash
sh> pwd
sh> ls > output.txt
sh> cat < input.txt
sh> ls | grep .c
sh> mkdir test && cd test
sh> pwd; ls
```

---

## Limitations

* Limited command parsing
* No advanced scripting support
* No job control

---

Your Name
