CFLAGS = \
	-std=gnu2x \
    -Wall -Wextra -Werror -pedantic \
    -Wpointer-arith \
    -Wno-unused-parameter \
    -Wno-gnu-conditional-omitted-operand \
    -Wno-macro-redefined \
    -Wp,-D_FORTIFY_SOURCE=3 \
    -fexceptions \
    -fpic \
    -fPIE \
    --param=ssp-buffer-size=4  \
    -m64 \
    -ftrapv \
    -pipe \
    -fstack-protector \
    -fsanitize=address \
    -fno-omit-frame-pointer \
    -mtune=generic \
    -O

.PHONY: check clean clean-coveage report-coverage

%.o: %.c %.h
	rm -f $(basename $<).gcda $(basename $<).gcno
	$(CC) $(CFLAGS) -v -c -o $@ $<

dedup: dedup.c alist.o clone.o map.o progress.o queue.o
	rm -f dedup.gcda dedup.gcno
	$(CC) $(CFLAGS) -o $@.unsigned $^
	codesign -s - -v -f --entitlements entitlement.plist dedup.unsigned
	mv dedup.unsigned dedup

check: CFLAGS += \
        -DNDEBUG \
        -ftest-coverage \
        -fprofile-arcs \
        -g \
        -Og
check: dedup
	cd test && make check

clean-coverage:
	find . -type f -name '*.gcda' -delete
	find . -type f -name '*.gcno' -delete
	find . -type f -name '*.gcov' -delete

clean: clean-coverage
	rm -f *.o
	rm -f dedup
	cd test && make clean

report-coverage:
	mkdir -p build/private/coverage
	geninfo . -o build/private/coverage.info
	genhtml build/private/coverage.info -o build/private/coverage

PREFIX ?= /usr/local

install: dedup
	install dedup $(PREFIX)/bin
	install dedup.1 $(PREFIX)/share/man/man1
