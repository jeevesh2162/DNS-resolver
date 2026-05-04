CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c17 -O2 -g -D_DEFAULT_SOURCE

SRC_DIR = src
OBJ_DIR = obj
TEST_DIR = tests

SRCS = $(wildcard $(SRC_DIR)/*.c)
# Exclude main.c from general object files for tests
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(filter-out $(SRC_DIR)/main.c, $(SRCS)))
MAIN_OBJ = $(OBJ_DIR)/main.o

TARGET = dns_resolver

TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c, $(TEST_DIR)/%, $(TEST_SRCS))

.PHONY: all clean test directories

all: directories $(TARGET)

directories:
	@mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: directories $(OBJS) $(TEST_BINS)
	@for test in $(TEST_BINS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done

$(TEST_DIR)/%: $(TEST_DIR)/%.c $(OBJS)
	$(CC) $(CFLAGS) $< $(OBJS) -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(TARGET).exe $(TEST_BINS) *.exe
