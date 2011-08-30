.section ".data"

.globl dt_blob_start
dt_blob_start:

.incbin "../source/lv2/linux/xenon.dtb" # relative to build dir

.globl dt_blob_end
dt_blob_end:
