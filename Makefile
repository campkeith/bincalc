TARGETS=bincalc

CFLAGS=-c -Wall -Werror -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS
all: ${TARGETS}

${TARGETS}: bincalc.o
	gcc $^ -lc -lstdc++ -lreadline -o $@

%.o: %.cpp
	gcc ${CFLAGS} $^ -o $@

.PHONY: clean

clean:
	rm -f *.o ${TARGETS}
