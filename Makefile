LANG = en

VERSION ?= 0.0.3

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
    -O \
    '-DVERSION="$(VERSION)"' \
    '-DBUILD_DATE="$(shell date '+%Y%m%d')"'

ENTITLEMENT_FLAGS =

.PHONY: check-build check clean clean-coveage report-coverage dist check-spelling check-spelling-man check-spelling-readme

%.o: %.c %.h
	rm -f $(basename $<).gcda $(basename $<).gcno
	$(CC) $(CFLAGS) -v -c -o $@ $<

dedup: dedup.c alist.o clone.o map.o progress.o queue.o
	rm -f dedup.gcda dedup.gcno
	$(CC) $(CFLAGS) -o $@.unsigned $^
	codesign -s - -v -f $(ENTITLEMENT_FLAGS) dedup.unsigned
	mv dedup.unsigned dedup

check-build: ENTITLEMENT_FLAGS = --entitlements entitlement.plist
check-build: CFLAGS += \
        -DNDEBUG \
        -ftest-coverage \
        -fprofile-arcs \
        -fsanitize-address-use-after-return=always \
        -g \
        -O0
check-build: dedup
check: check-build
	cd test && make check

clean-coverage:
	find . -type f -name '*.gcda' -delete
	find . -type f -name '*.gcno' -delete
	find . -type f -name '*.gcov' -delete

clean: clean-coverage
	rm -f *.o
	rm -f dedup
	cd test && make clean
	rm -rf build

report-coverage:
	mkdir -p build/private/coverage
	geninfo . -o build/private/coverage.info
	genhtml build/private/coverage.info -o build/private/coverage

PREFIX ?= /usr/local

install: dedup
	install dedup $(PREFIX)/bin
	install dedup.1 $(PREFIX)/share/man/man1

build/dist:
	mkdir -p $(PREFIX)/bin
	mkdir -p $(PREFIX)/share/man/man1

dist: PREFIX=build/dist
dist: check-spelling dedup build/dist install
	cd build/dist; tar -Jcvf dedup-$(VERSION).tar.xz bin share
	cd build/dist; zip -r dedup-$(VERSION).zip bin share

dist-verify: check dist
	mkdir -p build/dist-check
	cd build/dist-check && tar xvJf ../dist/dedup-$(VERSION).tar.xz
	cd build/dist-check && test -x bin/dedup
	cd build/dist-check && test -f share/man/man1/dedup.1

uninstall:
	rm $(PREFIX)/bin/dedup
	rm $(PREFIX)/share/man/man1/dedup.1

distcheck: PREFIX=build/dist-check
distcheck: dist-verify uninstall

OUTPUT_DICT = build/private-aspell-dict

$(OUTPUT_DICT): dict
	$(call print_target)
	mkdir -p build/private/spelling
	aspell --lang="$(LANG)" create master "./$@" < $^

check-spelling-man: dedup.1 $(OUTPUT_DICT)
	cat dedup.1 | \
        tr '”' ' ' | \
	    aspell list \
            --mode nroff \
            --lang="$(LANG)" \
            --extra-dicts="./$(OUTPUT_DICT)" | \
	    sort -u | \
	    xargs -n 1 printf "\\033[0;31m$< >>>\033[0m %s\n"

check-spelling-readme: README.md $(OUTPUT_DICT)
	cat README.md | \
	    aspell list \
	        --mode markdown \
	        --lang="$(LANG)" \
	        --extra-dicts="./$(OUTPUT_DICT)" | \
	    sort -u | \
	    xargs -n 1 printf "\\033[0;31m$< >>>\033[0m %s\n"

check-spelling: check-spelling-readme check-spelling-man

clonefile-bug: CFLAGS += \
        -DNDEBUG \
        -ftest-coverage \
        -fprofile-arcs \
        -fsanitize-address-use-after-return=always \
        -g \
        -O0
clonefile-bug: clonefile-bug.o clone.o
	$(CC) $(CFLAGS) -o $@ $^
