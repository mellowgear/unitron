# undocumented sequences come out as DEFB; ED 6B (a second encoding of LD HL,(nn)) too;
# an address inside an instruction is written label+k (LD (0017H),A; JR to its own operand);
# hints and --executed add code
z80 roundtrip roundtrip-workdir.bin --workdir out
