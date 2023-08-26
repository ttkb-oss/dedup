CFLAGS += \
    -I/opt/local/include \
	-std=c2x \
    -Wall -Wextra -Werror -pedantic \
    -Wpointer-arith \
    -Wno-unused-parameter \
    -Wp,-D_FORTIFY_SOURCE=2 \
    -fexceptions \
    -fpic \
    -fprofile-arcs \
    -Wno-macro-redefined \
    --param=ssp-buffer-size=4  \
    -grecord-gcc-switches \
    -m64 \
    -ftrapv \
    -pipe \
    -ftest-coverage \
    -fstack-protector \
    -fsanitize=address \
    -g \
    -Wno-gnu-zero-variadic-macro-arguments \
    -mtune=generic \
    -fPIE \
    -Og

LDFLAGS += \
    -L/opt/local/lib

.PHONY: setup check clean clean-test-data

%.o: %.c
	rm -f $(basename $<).gcda $(basename $<).gcno
	$(CC) $(CFLAGS) -c -o $@ $<

check: dedup_check setup
	./dedup_check && \
        hdiutil detach /Volumes/dedup-test-hfs

dedup_check: dedup_check.c dedup_suite.c clone_suite.o ../clone.o
	rm -f dedup_check.gcda dedup_check.gcno
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ -l check $^

clean-test-data:
	if [[ -d /Volumes/dedup-test-hfs ]]; then \
	    hdiutil detach /Volumes/dedup-test-hfs; \
	fi
	if [[ -f test-data/clone-dst-acls/.~.bar4 ]]; then \
	    chflags nouchg test-data/clone-dst-acls/.~.bar4; \
	fi
	if [[ -f test-data/clone-dst-acls/bar4 ]]; then \
	    chflags nouchg test-data/clone-dst-acls/bar4; \
	fi
	rm -rf test-data

setup: clean-test-data
	mkdir -p test-data/{bars,empty,devices,big,same-size,same-first-last,flags-acls,clone-dst-acls}
	# "bar" test data
	pushd test-data/bars; \
	    echo "foo" > bar; \
	    cp bar bar2; \
	    ln bar bar3; \
	    cp -c bar bar4; \
	    cp bar bar5;
	# "empty" test data
	pushd test-data/empty; \
	    touch empty; \
	    touch empty2; \
	# "devices" test data
	pushd test-data/devices; \
	    mkfifo fifo; \
        touch empty;
	# "big" test data
	pushd test-data/big; \
	    dd if=/dev/random of=big bs=1m count=4; \
	    dd if=/dev/random of=big2 bs=1m count=4;
	# "same-size" test data
	pushd test-data/same-size; \
	    dd if=/dev/zero of=big bs=1m count=1; \
	    dd if=/dev/zero of=big2 bs=1m count=1;
	# "same-first-last" test data
	pushd test-data/same-first-last; \
	    dd if=/dev/random of=big bs=1m count=4; \
	    dd if=/dev/random of=big2 bs=1m count=4; \
	    echo "a" > same-1; \
	    cat big >> same-1; \
	    echo "z" >> same-1; \
	    echo "a" > same-2; \
	    cat big2 >> same-2; \
	    echo "z" >> same-2;
	# "flags-acls" test data
	pushd test-data/flags-acls; \
	    echo "foo" > bar; \
	    ln bar bar2; \
	    cp bar bar3; \
	    chmod +a "nobody deny append" bar3; \
	    chmod 642 bar3;
	# "clone-dst-acls" test data
	pushd test-data/clone-dst-acls; \
	    echo "foo" > bar; \
	    ln bar bar2; \
	    cp bar bar3; \
	    chmod +a "$$USER deny readattr,readextattr" bar3; \
	    cp bar bar4; \
	    chflags uchg bar4;
	# HFS test
	hdiutil create \
	    -srcfolder test-data/bars \
	    -volname 'dedup-test-hfs' \
	    -fs HFS+ \
	    -format UDRW \
	    -size 1m \
	    test-data/bars.dmg
	device="$$(hdiutil attach \
	    -readwrite \
	    -noverify \
	    -noautoopen \
	    test-data/bars.dmg \
	    | egrep '^/dev/' \
	    | sed 1q \
	    | awk '{print $$1}')" && \
	    echo "$$device" >> test-data/bars-device

clean: clean-test-data
	rm -f *.o
	rm -f dedup_check
