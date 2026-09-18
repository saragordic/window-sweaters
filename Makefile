CFILES = src/main.c src/parse.c src/mach.c src/hashtable.c src/events.c src/reconcile.c \
         src/windows.c src/border.c src/animation.c src/knit.c src/chart.c src/apps.c
LIBS = -framework AppKit -framework Cocoa -framework CoreVideo -framework UniformTypeIdentifiers \
       -F/System/Library/PrivateFrameworks/ -framework SkyLight

# Released builds are universal so Intel Macs can run the download. Local
# development targets (debug, tests, probes) stay native for build speed.
ARCHS = -arch arm64 -arch x86_64
# Without this clang targets whatever macOS built it, and the binary then
# refuses to launch below that version regardless of LSMinimumSystemVersion.
DEPLOY = -mmacosx-version-min=13.0

.PHONY: all debug clean test preview catalogue bench

all: | bin
	clang $(ARCHS) $(DEPLOY) -O3 -g -Isrc -fobjc-arc -c src/menubar.m -o bin/menubar.o
	clang $(ARCHS) $(DEPLOY) -O3 -g -Isrc -fobjc-arc -c src/autoyarn.m -o bin/autoyarn.o
	clang $(ARCHS) $(DEPLOY) -O3 -g -Isrc -fobjc-arc -c src/hidden.m -o bin/hidden.o
	clang $(ARCHS) $(DEPLOY) -std=c99 -O3 -g -Isrc $(CFILES) bin/menubar.o bin/autoyarn.o bin/hidden.o -o bin/borders $(LIBS)

debug: | bin
	clang -O0 -g -Isrc -fobjc-arc -c src/menubar.m -o bin/menubar.o
	clang -O0 -g -Isrc -fobjc-arc -c src/autoyarn.m -o bin/autoyarn.o
	clang -O0 -g -Isrc -fobjc-arc -c src/hidden.m -o bin/hidden.o
	clang -std=c99 -O0 -g -DDEBUG -Isrc $(CFILES) bin/menubar.o bin/autoyarn.o bin/hidden.o -o bin/debug $(LIBS)

bin:
	mkdir bin

clean:
	rm -rf bin

bin/render-test: tests/render.c src/knit.c src/chart.c src/apps.c src/misc/knit.h src/misc/chart.h src/misc/apps.h tests/catalogue.h | bin
	clang -std=c99 -O3 -Isrc tests/render.c src/knit.c src/chart.c src/apps.c -framework ApplicationServices -framework CoreText -o $@

test: bin/render-test
	clang -std=c99 -O1 -g -fsanitize=address,undefined -Isrc tests/tracking.c src/animation.c -o bin/tracking-test $(LIBS)
	bin/tracking-test
	clang -std=c99 -O1 -g -fsanitize=address,undefined -Isrc tests/events.c src/hashtable.c -o bin/events-test $(LIBS)
	bin/events-test
	clang -O1 -g -fobjc-arc -fsanitize=address,undefined -Isrc tests/reconcile.m src/hashtable.c -o bin/reconcile-test $(LIBS)
	bin/reconcile-test
	clang -std=c99 -Wall -Wextra -Werror -fsanitize=address -g -Isrc tests/collection_test.c src/apps.c src/chart.c -o bin/collection-test -framework ApplicationServices
	bin/collection-test
	clang -Wall -Wextra -Werror -fobjc-arc -fsanitize=address,undefined -g -Isrc tests/hidden_apps.m src/hidden.m -o bin/hidden-apps-test -framework AppKit
	bin/hidden-apps-test
	clang -O1 -g -fobjc-arc -fsanitize=address,undefined -Isrc tests/app_filter.m src/hashtable.c -framework AppKit -o bin/app-filter-test
	bin/app-filter-test
	bin/render-test
	clang -std=c99 -O1 -g -fsanitize=address,undefined -Isrc tests/ax_focus.c -framework ApplicationServices -o bin/ax-focus-test
	bin/ax-focus-test
	clang -std=c99 -O1 -g -fobjc-arc -fsanitize=address,undefined -Isrc tests/menu.m src/hidden.m -framework Cocoa -framework UniformTypeIdentifiers -o bin/menu-test
	bin/menu-test
	clang -O1 -g -fobjc-arc -fsanitize=address,undefined -Isrc tests/autoyarn.m src/knit.c src/chart.c src/apps.c -framework Cocoa -framework ApplicationServices -o bin/autoyarn-test
	bin/autoyarn-test

# Optional native integration probe: build only. Running it creates its own
# temporary window; keep it out of unattended unit tests.
bin/live-resize: tests/live_resize.m src/border.c src/border.h src/knit.c src/chart.c src/apps.c | bin
	clang -O2 -g -fobjc-arc -Isrc tests/live_resize.m src/animation.c src/knit.c src/chart.c src/apps.c -o $@ $(LIBS)

bin/live-order: tests/live_order.m tests/live_resize.m src/border.c src/border.h src/knit.c src/chart.c src/apps.c | bin
	clang -O2 -g -fobjc-arc -Isrc tests/live_order.m src/animation.c src/knit.c src/chart.c src/apps.c -o $@ $(LIBS)

preview: bin/render-test
	mkdir -p outputs
	bin/render-test outputs/collection-preview.png

catalogue: bin/render-test
	mkdir -p docs/collection
	bin/render-test --catalogue docs/collection
	bin/render-test --classics docs/collection
	bin/render-test --individual docs/collection

bench: bin/render-test
	bin/render-test --bench

# Optional owned-window display handoff probe; --list is read-only.
bin/live-display: tests/live_display.m tests/live_resize.m src/border.c src/border.h src/knit.c src/chart.c src/apps.c | bin
	clang -O2 -g -fobjc-arc -Isrc tests/live_display.m src/animation.c src/knit.c src/chart.c src/apps.c -o $@ $(LIBS)
