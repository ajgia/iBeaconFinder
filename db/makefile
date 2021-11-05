SOURCES = main.c
OBJECTS = main.o
BINARY = tester
CFLAGS += -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -g
LDFLAGS += -ldc_error -ldc_posix

.PHONY: all
all: $(BINARY)

.PHONY: tester
$(BINARY) : $(OBJECTS)
	$(CC) -o $(BINARY) $(OBJECTS) $(LDFLAGS) $(LDLIBS)

$(OBJECTS):

.PHONY: lint
lint:
	$(LINT) $(LINTFLAGS) $(SOURCES) --

.PHONY: clean
clean:
	rm -f $(BINARY) $(OBJECTS)

.PHONY: run
run: tester
	./$(BINARY)

.PHONY: run-memcheck
run-memcheck: $(BINARY)
	$(MEMECHECK) $(MEMCHECK_FLAGS) ./$(BINARY)
