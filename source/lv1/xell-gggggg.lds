     ENTRY(_start)
     SECTIONS
     {
        . = 0x800000001c000000;
       .text : { *(.text) }
       .data : { *(.data) }
       .sdata : { *(.sdata) }
       .rodata : { *(.rodata)}

        . = 0x800000001c080000;

       bss_start = .;
       .bss : { *(.bss) }
       .sbss : { *(.sbss) }
       bss_end = .;
     }
