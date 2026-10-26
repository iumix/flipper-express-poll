# Host unit tests for the hardware-independent modules.
# Usage: make -C tests        (builds with sanitizers and runs)

CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS ?= -fsanitize=address,undefined

SRCS = test_express_poll.c \
       ../nfca/ecp_frames.c \
       ../felica/felica_frames.c \
       ../felica/felica_systems.c \
       ../balance/balance.c \
       ../balance/balance_suica.c \
       ../balance/balance_octopus.c

BUILD = build
BIN = $(BUILD)/test_express_poll

.PHONY: test clean
test: $(BIN) $(BUILD)/test_emv_select $(BUILD)/test_ksx6924
	./$(BIN)
	./$(BUILD)/test_emv_select
	./$(BUILD)/test_ksx6924

$(BIN): $(SRCS) $(wildcard ../felica/*.h ../balance/*.h ../nfca/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD)

ISO_DEP = ../nfca/iso_dep.c ../nfca/iso_dep.h

$(BUILD)/test_emv_select: test_emv_select.c ../nfca/emv_select.c ../nfca/emv_select.h $(ISO_DEP)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) test_emv_select.c ../nfca/emv_select.c ../nfca/iso_dep.c $(LDFLAGS) -o $@

$(BUILD)/test_ksx6924: test_ksx6924.c ../nfca/ksx6924.c ../nfca/ksx6924.h $(ISO_DEP)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) test_ksx6924.c ../nfca/ksx6924.c ../nfca/iso_dep.c $(LDFLAGS) -o $@
