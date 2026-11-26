# Playing a game to map it: guidance for agents

How to use `unitron play` (docs/play.md) to map a game while disassembling it, at the lowest
cost in tokens and with results anyone can reproduce. Written for AI agents doing the playing;
it works the same for people.

## The rules

1. **Play from routes, not by hand.** Every state you keep comes from a route file
   (`play script`): inputs, `until` waits, `expect` checks, `save`. Routes live with the game's
   project, are kept like source, and rebuild every state from power-on. `play run` is for
   one-off experiments; when an experiment works, put it in a route.
2. **Wait on RAM, not on pictures.** Find the RAM value that marks a moment (a menu cursor, a
   screen id, a chip count, an HP value) and `until` on it, with `each` doing the mashing.
   One command, one line of output, instead of a loop of screenshots.
3. **Text before images.** Read the game's state as text: your status script (below), `play
   tiles`, a RAM dump. Take a screenshot only for a screen you have not seen before, or to
   confirm what a RAM value means; then write down what you learned so you never need that
   picture again. Look at screenshots in montages (`play montage`, 4 x 4 at most), not singly.
4. **Check as you go.** Put `expect` lines in routes for what you know (HP, money, the chapter).
   A route that still passes its checks is a route that still works; one that fails says where.
5. **Everything is replayable.** States carry their input log from power-on; a trace can always
   be rebuilt by replaying (`play run --inputs`). Never depend on something you can't recreate.

## Your tools vs unitron's

unitron gives the general machinery: the emulator, states, routes, conditions, traces, hints.
**Each game needs scripts of its own**, written by the agent in the game's project (not in
unitron), and they are part of the job, not a side task:

- **A status script**: one line of text per state from RAM: what screen, the player's numbers,
  where things are. It grows as RAM is mapped, and it replaces most screenshots.
- **A text reader**: the game's text encoding (from its strings in the ROM, or its tile font with
  `play tiles`), so dialogue and menus can be read as text.
- **Route helpers**: whatever makes routes short for this game (menu walks, map movement,
  battle turns), generating route steps or route files.
- **RAM and data notes**: a table of RAM addresses with evidence, and a play journal.

The state and trace files are documented (docs/play.md) so these scripts can be written in any
language.

## The loop

1. Write or extend a route to reach the next moment; run it; read its one-line results.
2. Learn something: diff RAM between two states around one action, find a value by search, and
   find its code with `play trace --addr ADDR --sym FILE`.
3. Write it down (RAM notes, journal); teach the status script about it; add an `expect` for it.
4. See what is left: `play trace --unnamed ram/wram.inc --sym FILE` lists every RAM address the
   game used that still has no name, with the routines that use it.
5. Feed the trace back: `play hints`, then `unitron dis --hints --executed`, and name what the
   evidence supports.
