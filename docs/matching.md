# Matching the original compiler output

How the sources in `src/` were made to compile to the original bytes, and what to keep in mind when editing them
without losing the match. Compiler: MPW ARM C 4.62 (Norcroft), run through `tools/mpwrun.py`.

## Workflow

1. Read the original disassembly of the function first for its control-flow shape (branch layout, register use),
   then write C that produces that shape.
2. Compile one file: `tools/cc.sh <name>` -> `build/<name>.o`.
3. Compare per function: `python3 tools/cmp_obj.py extracted/<program> build/<name>.o -v` (needs capstone for
   `-v`). Relocated words are wildcards.
4. Compare whole areas, including strings and data: `python3 tools/cmp_area.py extracted/<program> build/<name>.o
   <code addr> [<data addr> [<zidata addr>]]`.
5. Rebuild the program and compare the whole file: `tools/build_launchme.sh`, `tools/build_ode.sh`, `tools/build_mplay.sh`, `tools/build_storagetuner.sh`.

One compile takes about a second, so trying combinations of candidate source forms by script is practical.

## Flags and file layout

- `armcc -fa -zps0 -za1 -g -d DEBUG=1`, run from the source folder with the file given as `:name.c`
  (`__FILE__` is `":name.c"`). Without `-g` or without `DEBUG=1` the code differs.
- Object boundaries: string literals used by data initialisers come at the start of an object's code area, and all
  const data (file-scope tables, local array initialiser templates) at the end, in definition order. A function
  loads literals only from its own object's area, so shared literal pools show which functions share a file.
- With `-g`, a function with an explicit `return` statement is followed by a duplicate epilogue word. A `void`
  function without `return;` has none; adding `return;` adds it.
- armlink orders code areas by name (ASMCODE, then C$$code in command-line order, then STUB areas), stable in input
  order. Initialised statics are laid out in definition order; uninitialised statics go to the zero-init area, so
  statics that sit in the data area need an explicit `= 0`.

## Locals and registers

- Scalars: the first declared local gets the lowest callee-saved register.
- Arrays and other address-taken locals: the first declared gets the higher stack address.
- A declared but unused local still gets a register or stack slot; unused aggregates still take stack space.
- A dead initialised local (`uint8 *p = buf;`, never read) still loads its register.
- Parameters can be reused as locals (`buttons = read_pad();`).
- Register choice can depend on code elsewhere in the same function. In the ode driver, adding a redundant
  `& 0xff` in three byte copies (`ioi_Unit = rw->unit & 0xff`) fixed register choices in other statements.
  `cmd[2] = x & 0xff` and `cmd[2] = x` compile to the same instructions but allocate registers differently.

## Statements and control flow

- `if (x) goto out;` gives `bxx L; b out`; `if (!x) { ... }` gives one branch.
- An empty `if (err < 0) ;` still emits a branch.
- `for (..; a; ..) { if (b || c) break; ... }` differs from a combined `a && b && c` condition.
- Single-case `switch (x) { case 0: ... break; default: ... }` gives `teq; beq L; b default`, with a branch to the
  next instruction at the end of each case. Case order follows source order.
- Unreachable code after an endless loop (`return 0;`) is still emitted.
- `if (err != 0 || (err = p->io_Error) != 0)` gives `teq; bne; ldr; movs; beq`, unlike `if (!err) err = ...;
  if (err)`.
- Constant arithmetic written as separate steps stays separate (`ver -= 60; ver -= 9;` is not `ver == 69`).

## Initialisers and calls

- `IOInfo ioi = {0};` zeroes inline with `stm`; other forms call memset.
- `uint8 cmd[8] = {0};` as a local becomes an 8-byte template in the code area, copied with `ldm`/`stm`.
- Arguments of the same call are evaluated in an order that depends on their types and form: seven separate
  `char[32]` arrays and one `char[7][32]` array give different orders.
- Whether a function is called through a library stub or an inline SWI depends on the declaration in scope
  (e.g. `kprintf` with or without the `__swi` prototype from `debug.h`).
- Line numbers matter where `__LINE__` is used (SDK `DIAGNOSE` / `DEBUG3DO_BANNER` macros): the call must stay on
  the same source line.
