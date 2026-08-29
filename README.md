# clangtidy

clang-tidy as an mcpp build rule. One graph edge per file: incremental,
parallel, and attributable to the file that failed rather than to
"the build program exited 1".

```toml
# mcpp.toml
[build-dependencies]
clangtidy = { version = "0.1.0", host-module = true }
llvm      = { version = "...",   tools = ["clang-tidy"] }
```

```cpp
// build.mcpp
import std;
import mcpp;
import clangtidy;

int main() {
    std::vector<std::string> files{ "src/main.cpp", "src/other.cpp" };
    return clangtidy::check(files) ? 0 : 1;
}
```

That is the whole of it. `${mcpp.compile_db}` — the `-p` argument clang-tidy
wants — is filled in by mcpp, so the rule never has to know where the build
directory is.

## Two things this package exists to take off you

**clang-tidy exits 0 while reporting a diagnostic.** Measured:

```
main.cpp:6:5: warning: the result from calling 'memcpy' is not null-terminated
              [bugprone-not-null-terminated-result]
clang-tidy exit=0
```

A check's contract is that the exit code is the verdict, so a rule that simply
runs clang-tidy produces a check that passes on every input it will ever see,
while the finding scrolls past in a green build. `warnings_are_errors` is on by
default; turn it off only to make the checks advisory on purpose, and know that
the edge then cannot fail.

**A check's stamp.** The build graph needs a file to mark the edge satisfied,
and clang-tidy writes none. mcpp creates it when the command exits zero — so
there is no wrapper script here, and none in your project either.

> ⚠️ **Requires mcpp >= 2026.8.29.1.** Before that release the *command* had to
> create the stamp, and an action's command is an argv with no shell assumed —
> correct for Windows, and leaves nothing there to touch a file with. That is
> what made a portable clang-tidy rule impossible rather than merely awkward.

## Layers

Each is the composition of the one below it, so there is no cliff to fall off:

```cpp
clangtidy::check(files)                       // L0
clangtidy::check(files, {.args = {"--checks=-*,bugprone-*"}})   // L1
clangtidy::check(files, {.blocking = true})   // L2: gate compilation on it
auto e = clangtidy::plan(files); /* edit */ ; clangtidy::submit(e);   // L3
```

`check(files, opt)` *is* `submit(plan(files, opt))`.

**Checks run beside compilation by default.** Serialising a whole build behind
a linter costs more than it saves, and a failing check fails the build either
way. `blocking = true` is for the case where a failure means the compile was
wasted anyway.

## What this does not do

Print command lines. mcpp writes every action's full argv into `build.ninja`,
recoverable with `ninja -t commands <output>`; a second copy would only drift.
What the rule owns is the other half — which knobs produced the command — and
that goes in each edge's description.
