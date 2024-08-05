#For C++ change to -std=c++20
CFLAGS=-Wall -Wextra -pedantic -g -std=c99 -Og -fsanitize=undefined
FFLAGS=-DFAULT_MODULE_MAX=3 -DFAULT_ID_MAX=10 -DFAULT_LOG_MAX=2
LFLAGS=-lubsan
TARGET=tests


%.o : %.c
	$(CC) $(CFLAGS) $(FFLAGS) -c $<

$(TARGET) : main.o faults.o
	$(CC) -o $@ $^ $(LFLAGS)

runtests: $(TARGET)
runtests:
	./$(TARGET)

clean:
	$(RM) $(TARGET) *.o

release: CFLAGS=-Wall -Wextra -pedantic -g -std=c99 -O2 -DNDEBUG
release: LFLAGS=-lm
release: clean
release: $(TARGET)

lint:
	cppcheck --enable=warning,style,performance,portability,unusedFunction .

