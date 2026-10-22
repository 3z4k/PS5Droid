/* A single position-independent RX segment, aligned for PS5's larger pages. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(ps5droid_fixture_entry)
PHDRS { code PT_LOAD FILEHDR PHDRS FLAGS(5); }
SECTIONS {
    . = SIZEOF_HEADERS;
    .text : { *(.text*) } :code
    .rodata : { *(.rodata*) } :code
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash) }
}
