# The gap sweep past the last traced instruction (docs/language.md, "Trace hints"). The trace
# reaches start and tail; early and late are reached only through table (call XHL), and
# late2 only from late. late is after every traced instruction: the sweep takes it because
# table also points at early, which it found between traced code. lone_ptr points at bytes that
# decode as code (ld A,0x10 / ret) but sits in no table with a known routine, and the bytes
# after late2's ret decode as code too: both stay data.
asm sweep-late.asm -o out.ngp --origin 0x200000 --size 0x200 --no-mpp --sym out.sym
dis out.ngp -o out
