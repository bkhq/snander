TARGET ?= snander
CFLAGS ?= -Wall -s
LDFLAGS ?= $(shell pkg-config --cflags --libs libusb-1.0 2>/dev/null || echo -I/usr/include/libusb-1.0 -lusb-1.0)

FILES += src/main.o src/flashcmd_api.o src/timer.o \
	src/spi_controller.o src/spi_nand_flash.o src/spi_nor_flash.o \
	src/ch341a_i2c.c src/ch341a_spi.c

all: clean $(FILES)
	$(CC) $(CFLAGS) $(FILES) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f src/*.o
