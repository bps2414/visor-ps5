Homebrew loader from [ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv)
(commit 7748df9, GPL-3.0-or-later, by John Törnblom and contributors).

`hbldr.c`, `elfldr.c` and `pt.c` launch an ELF as a regular PS5 app, which is
what gives it video output and controller input. Changes from upstream:

- `hbldr.c`: the fake app is `VISR00002` / "Visor" instead of `FAKE00000` /
  "Homebrew Loader".
- `fs.h`, `sys.h`, `websrv.h`: stubs for the headers `hbldr.c` includes.
