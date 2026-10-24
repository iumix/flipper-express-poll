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
test: $(BIN) $(BUILD)/test_emv_select
	./$(BIN)
	./$(BUILD)/test_emv_select

$(BIN): $(SRCS) $(wildcard ../felica/*.h ../balance/*.h ../nfca/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD)

$(BUILD)/test_emv_select: test_emv_select.c ../nfca/emv_select.c ../nfca/emv_select.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) test_emv_select.c ../nfca/emv_select.c $(LDFLAGS) -o $@
