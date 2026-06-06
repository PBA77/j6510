# Klaus Dormann 6502 Functional Test

Files in this directory are copied from:

https://github.com/Klaus2m5/6502_65C02_functional_tests

The local `klaus_functional_test` runner loads `6502_functional_test.bin` as a
full 64 KB image, starts execution at `$0400`, and treats the final success trap
at `$3469` as pass.

The upstream `readme.txt` notes that `6502_functional_test.a65` tests all valid
opcodes and addressing modes of the original NMOS 6502 CPU. It also notes that
the interrupt test requires a feedback register to inject IRQ and NMI requests,
so it is not wired into this project yet.

`6502_decimal_test.bin` and `6502_decimal_test.a65` are copied from
`analog-hors/pones`, which includes assembled Klaus/Bruce Clark decimal test
assets derived from the same upstream suite.
