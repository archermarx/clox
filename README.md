# clox

My implementation of the Lox programming language in C, following [**Crafting Interpreters** by Robert Nystrom](https://craftinginterpreters.com).

## Compiling

Clone the source and run make. The `clox` executable is created in the `build` folder.

## Running

`clox` can be run in interactive mode by passing no arguments. An example of interactive mode is shown below

```
clox> print 2 + 2;
4
clox> fun double(x) {return x * 2;}
clox> print double(2);
4
```

My implementation of `clox` uses [`linenoise`](https://github.com/antirez/linenoise) to handle terminal interaction.
This means the left- and right- arrow keys can be used to edit input, and the up- and down- arrow keys can be used to page through your history.

Alternatively, you can run `clox` on a file by passing the file name as the first argument, i.e `clox my_program.lox`.

## Differences in my implementation

My implementation differs from the canonical one in a few ways

- Underscores can be used as separator characters in numeric literals
- `print` is a built-in function instead of a statement and does not append a newline character. `print` also takes multiple arguments, which are concatenated.
- Added a companion `println` function which works exactly like print, but appends a newline to the end.

## Example programs

Several example programs, most taken from the book, can be found in the `examples` folder.

## Tests

There are some automated tests in `test.c` which can be run by passing the `--test` flag to clox.
These tests are not exhaustive but cover some aspects of lexing, codegen, and hash tables.