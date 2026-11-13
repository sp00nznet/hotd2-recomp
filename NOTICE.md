# Notice

The MIT license in [LICENSE](LICENSE) covers what is ours in this repository:
the host, the lift driver and its patches, the setup scripts, the tools and
the documentation. It does not cover the game.

*The House of the Dead 2* is © 1998-2001 SEGA, and is not ours to license.
Its executables, data, movies, music and art are not in this repository and
are excluded by `.gitignore`.

- You need your own copy of the game to use any of this. Setup copies your
  install into `game/`; the pipeline reads it from there.
- Lifted code is a derivative work of the binary it came from. Anything the
  pipeline writes into `work/` or `src/recomp/gen*/` carries whatever license
  the original carries, not this one. The MIT grant stops at our own work.

The host is derived from the [House of the Dead recomp](https://github.com/sp00nznet/hotd-recomp)
and builds against [pcrecomp](https://github.com/sp00nznet/pcrecomp); both are
our own, MIT. No third-party code is in this repository.
