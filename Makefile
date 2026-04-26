BINARY = reverse-words
 
SRC = src
BUILD = build
PKG = $(BUILD)/reverse-words-project
 
BIN_DIR = $(PKG)/usr/local/bin
DEBIAN_DIR = $(PKG)/DEBIAN
 
CONTROL_FILE = DEBIAN/control
 
CC = gcc
CFLAGS = -Wall -Wextra -std=c11
 
all: check deb
 
check:
	@which $(CC) > /dev/null || (echo "gcc не установлен. Установите build-essential" && exit 1)
 
$(BINARY):
	$(CC) $(CFLAGS) $(SRC)/main.c -o $(BINARY)
 
deb: $(BINARY)
	rm -rf $(BUILD)
	mkdir -p $(BIN_DIR)
	mkdir -p $(DEBIAN_DIR)
 
	cp $(BINARY) $(BIN_DIR)/
	cp $(CONTROL_FILE) $(DEBIAN_DIR)/
 
	dpkg-deb --build $(PKG)
	@echo "Пакет создан: $(PKG).deb"
 
run: $(BINARY)
	./$(BINARY)
 
clean:
	rm -f $(BINARY)
	rm -rf $(BUILD)
