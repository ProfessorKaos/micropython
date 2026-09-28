make BOARD=MELD clean
make BOARD=MELD
esptool erase-flash
esptool write-flash 0 build-MELD/firmware.bin