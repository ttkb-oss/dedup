LANG = en

VERSION ?= 0.0.5

CFLAGS += \
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
    -fno-omit-frame-pointer \
    -mtune=generic \
    -O \
    '-DVERSION="$(VERSION)"' \
    '-DBUILD_DATE="$(shell date '+%Y%m%d')"'

ENTITLEMENT_FLAGS =

.PHONY: check-build check clean clean-coveage report-coverage dist universal-dist \
    check-spelling check-spelling-man \
	check-spelling-readme universal-dedup

%.o: %.c %.h
	rm -f $(basename $<).gcda $(basename $<).gcno
	$(CC) $(CFLAGS) -v -c -o $@ $<

dedup.arm: CFLAGS += -target arm64-apple-macos13
dedup.x86_64: CFLAGS += -target x86_64-apple-macos13

dedup dedup.arm dedup.x86_64: dedup.o alist.o clone.o map.o progress.o queue.o utils.o
	rm -f dedup.gcda dedup.gcno
	$(CC) $(CFLAGS) -o $@ $^
	mv $@ $@.unsigned
	codesign -s - -v -f $(ENTITLEMENT_FLAGS) $@.unsigned
	mv $@.unsigned $@

dedup.universal:
	rm -f *.o
	$(MAKE) dedup.arm
	rm -f *.o
	$(MAKE) dedup.x86_64
	lipo -create -output dedup.universal dedup.arm dedup.x86_64

universal-dedup: dedup.universal
	codesign -s - -v -f $(ENTITLEMENT_FLAGS) dedup.universal
	cp -c dedup.universal dedup

# leaks-build is a debug build without ASAN to be used with `leaks(1)`,
# Instruments, and other tools that don't want to evaluate libasan because it
# isn't a system library.
leaks-build: ENTITLEMENT_FLAGS = --entitlements entitlement.plist
leaks-build: CFLAGS += \
        -DNDEBUG \
        -ftest-coverage \
        -fprofile-arcs \
        -g \
        -O0
leaks-build: dedup
	dsymutil dedup

check-build: CFLAGS += \
        -fsanitize=address \
        -fsanitize-address-use-after-return=always
check-build: leaks-build
check: check-build
	cd test && make check

clean-coverage:
	find . -type f -name '*.gcda' -delete
	find . -type f -name '*.gcno' -delete
	find . -type f -name '*.gcov' -delete

clean: clean-coverage
	rm -f *.o
	rm -f dedup dedup.arm dedup.x86_64 dedup.universal
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
dist: check-spelling universal-dedup build/dist install
	cd build/dist; tar -Jcvf dedup-$(VERSION).tar.xz bin share
	cd build/dist; zip -r dedup-$(VERSION).zip bin share

dist-verify: check dist
	mkdir -p build/dist-check
	cd build/dist-check && tar xvJf ../dist/dedup-$(VERSION).tar.xz
	cd build/dist-check && test -x bin/dedup
	cd build/dist-check && test -f share/man/man1/dedup.1
	cd build/dist-check && file bin/dedup | grep -q 'Mach-O universal'
	cd build/dist-check && file bin/dedup | grep -q '(for architecture x86_64)'
	cd build/dist-check && file bin/dedup | grep -q '(for architecture arm64)'

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
