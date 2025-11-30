# SuperTerminal Makefile
# Convenient wrapper around CMake and the build script

# Default target
.PHONY: all
all: app

# Primary app bundle target (default)
.PHONY: app
app:
	@./build.sh

# Build configurations
.PHONY: release debug
release:
	@./build.sh

debug:
	@./build.sh --debug

# Clean target
.PHONY: clean
clean:
	@./build.sh --clean

# App bundle targets
.PHONY: interactive app-bundle package
interactive: app

app-bundle: app

package: app
	@echo "Creating installer package..."
	@productbuild --component build/SuperTerminal.app /Applications build/SuperTerminal-Interactive-1.0.0.pkg
	@echo "✓ Package created: build/SuperTerminal-Interactive-1.0.0.pkg"

# Specific targets
.PHONY: framework luarunner tests
framework:
	@./build.sh framework

luarunner:
	@./build.sh luarunner

tests:
	@./build.sh tests

# Deep clean
.PHONY: distclean
distclean:
	@rm -rf build/
	@echo "All build artifacts removed"

# Development targets
.PHONY: rebuild
rebuild:
	@./build.sh --rebuild

# Testing targets
.PHONY: test test-raw test-audio
test: tests
	@echo "Running basic tests..."
	@cd build && ./test_raw_lua
	@echo "✓ Raw Lua test passed"

test-raw: tests
	@cd build && ./test_raw_lua

test-audio: tests
	@cd build && ./test_audio_shutdown_diagnostic

# Installation targets
.PHONY: install install-app install-framework
install: install-app

install-app: package
	@echo "Installing SuperTerminal.app to /Applications..."
	@sudo installer -pkg build/SuperTerminal-Interactive-1.0.0.pkg -target /
	@echo "✓ SuperTerminal installed to /Applications/"

install-framework: framework
	@cd build && make install

# Development helpers
.PHONY: run run-app run-luarunner run-demo run-interactive
run: app
	@open build/SuperTerminal.app

run-app: run

run-luarunner: luarunner
	@cd build && ./luarunner

run-demo: demos
	@cd build && ./sprites_demo

run-interactive: app
	@open build/SuperTerminal.app

# Skia build (optional)
.PHONY: build-skia
build-skia:
	@echo "Building Skia (this may take a while)..."
	@cd skia && python tools/git-sync-deps
	@cd skia && bin/gn gen out/Release --args='is_official_build=true is_component_build=false'
	@cd skia && ninja -C out/Release
	@echo "✓ Skia build completed"

# Help target
# App Store targets
.PHONY: app-store app-store-clean app-validate app-package app-upload
app-store:
	@./build_app_store.sh

app-store-clean:
	@./build_app_store.sh --clean

app-validate:
	@./build_app_store.sh --validate

app-package: app-store
	@echo "Creating installer package..."
	@cd build_app_store && \
	productbuild --component SuperTerminal.app /Applications SuperTerminal-1.0.0.pkg
	@echo "✓ Package created: build_app_store/SuperTerminal-1.0.0.pkg"

app-upload:
	@echo "Upload requires team ID and credentials. Use:"
	@echo "  ./build_app_store.sh --upload --team-id YOUR_TEAM_ID"

app-test: app-store
	@./test_app_bundle.sh

.PHONY: help
help:
	@echo "SuperTerminal Makefile"
	@echo ""
	@echo "Primary targets:"
	@echo "  all, app         - Build SuperTerminal.app (interactive runner) - DEFAULT"
	@echo "  run              - Build and run SuperTerminal.app"
	@echo "  package          - Create installer package (.pkg)"
	@echo "  install          - Install SuperTerminal to /Applications"
	@echo ""
	@echo "Development targets:"
	@echo "  release          - Build everything in Release mode"
	@echo "  debug            - Build everything in Debug mode"
	@echo "  framework        - Build only the SuperTerminal framework"
	@echo "  luarunner        - Build command-line Lua runner"
	@echo "  tests            - Build test executables"
	@echo "  demos            - Build demo executables"
	@echo "  clean            - Clean build directory"
	@echo "  distclean        - Remove all build artifacts"
	@echo "  rebuild          - Clean and build everything"
	@echo "  test             - Run basic tests"
	@echo "  build-skia       - Build Skia graphics library (optional)"
	@echo ""
	@echo "Legacy targets:"
	@echo "  run-luarunner    - Build and run command-line Lua runner"
	@echo "  run-demo         - Build and run sprite demo"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "App Store targets:"
	@echo "  app-store       - Build app bundle for App Store"
	@echo "  app-store-clean - Clean build and create app bundle"
	@echo "  app-validate    - Build and validate app for App Store"
	@echo "  app-package     - Create installer package"
	@echo "  app-test        - Build and test app bundle functionality"
	@echo ""
	@echo "Examples:"
	@echo "  make            # Build SuperTerminal.app (default)"
	@echo "  make run        # Build and launch SuperTerminal.app"
	@echo "  make package    # Create installer package"
	@echo "  make install    # Install to /Applications"
	@echo "  make clean      # Clean build files"
	@echo "  make test       # Run tests"
	@echo ""
	@echo "App Store submission:"
	@echo "  make app-store  # Build for App Store"
