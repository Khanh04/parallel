# Fixed Makefile for LibClang Loop Analyzer on macOS
# This version resolves header path issues

# Use system clang++ instead of Homebrew clang++
CXX = /usr/bin/clang++
CXXFLAGS = -std=c++17 -Wall -Wextra

# Detect system and set appropriate flags
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    # macOS - use system clang with Homebrew libclang
    MACOS_SDK := $(shell xcrun --show-sdk-path 2>/dev/null)
    HOMEBREW_LLVM := $(shell brew --prefix llvm 2>/dev/null)
    
    # Add system root and headers
    ifneq ($(MACOS_SDK),)
        CXXFLAGS += -isysroot $(MACOS_SDK)
        CXXFLAGS += -I$(MACOS_SDK)/usr/include
    endif
    
    # Add LLVM headers and libraries  
    ifneq ($(HOMEBREW_LLVM),)
        CXXFLAGS += -I$(HOMEBREW_LLVM)/include
        LDFLAGS += -L$(HOMEBREW_LLVM)/lib
        LIBS = -lclang
    else
        # Fallback to system paths
        CXXFLAGS += -I/usr/local/include
        LDFLAGS += -L/usr/local/lib
        LIBS = -lclang
    endif
    
    # Additional macOS flags
    CXXFLAGS += -mmacosx-version-min=10.15
else
    # Linux
    LLVM_CONFIG = $(shell which llvm-config-14 || which llvm-config-13 || which llvm-config-12 || which llvm-config || echo "")
    ifneq ($(LLVM_CONFIG),)
        CXXFLAGS += $(shell $(LLVM_CONFIG) --cxxflags)
        LDFLAGS += $(shell $(LLVM_CONFIG) --ldflags)
        LIBS = -lclang
    else
        # Fallback for systems without llvm-config
        CXXFLAGS += -I/usr/include/clang-c
        LIBS = -lclang
    endif
endif

TARGET = loop_analyzer
SOURCE = loop_parallelizer.cpp
SAMPLE_INPUT = sample_loops.cpp
SAMPLE_OUTPUT = sample_loops_openmp.cpp

.PHONY: all clean test install check-deps debug info

all: check-deps $(TARGET)

$(TARGET): $(SOURCE)
	@echo "Building $(TARGET)..."
	@echo "Using compiler: $(CXX)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "LIBS: $(LIBS)"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET) $(SOURCE) $(LIBS)
	@echo "Build successful!"
	@ls -la $(TARGET)

check-deps:
	@echo "Checking dependencies..."
	@which $(CXX) >/dev/null 2>&1 || (echo "Error: $(CXX) not found."; exit 1)
	@echo "Using compiler: $(shell $(CXX) --version | head -n1)"
	@echo "Checking for libclang..."
ifeq ($(UNAME_S),Darwin)
	@(ls /usr/local/lib/libclang.* >/dev/null 2>&1 || ls $(HOMEBREW_LLVM)/lib/libclang.* >/dev/null 2>&1) || \
	 (echo "Error: libclang not found. Install with: brew install llvm"; exit 1)
	@echo "Found libclang at: $(shell ls $(HOMEBREW_LLVM)/lib/libclang.* 2>/dev/null | head -n1)"
else
	@(ldconfig -p | grep libclang >/dev/null 2>&1) || \
	 (echo "Error: libclang not found. Install with: sudo apt-get install libclang-dev"; exit 1)
endif
	@echo "Dependencies OK!"

info:
	@echo "Build Information:"
	@echo "OS: $(UNAME_S)"
	@echo "Compiler: $(CXX)"
	@echo "SDK Path: $(MACOS_SDK)"
	@echo "LLVM Path: $(HOMEBREW_LLVM)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "LIBS: $(LIBS)"

test: $(TARGET)
	@echo "Running test with sample code..."
	@echo "Creating sample file if it doesn't exist..."
	@./$(TARGET) $(SAMPLE_INPUT) 2>/dev/null || ./$(TARGET)
	@echo "Test completed!"

# Simple test to verify libclang works
test-libclang:
	@echo "Testing libclang integration..."
	@echo '#include <clang-c/Index.h>' > test_simple.cpp
	@echo '#include <iostream>' >> test_simple.cpp
	@echo 'int main() {' >> test_simple.cpp
	@echo '    CXIndex index = clang_createIndex(0, 0);' >> test_simple.cpp
	@echo '    if (index) {' >> test_simple.cpp
	@echo '        std::cout << "libclang works!" << std::endl;' >> test_simple.cpp
	@echo '        clang_disposeIndex(index);' >> test_simple.cpp
	@echo '        return 0;' >> test_simple.cpp
	@echo '    }' >> test_simple.cpp
	@echo '    std::cout << "libclang failed" << std::endl;' >> test_simple.cpp
	@echo '    return 1;' >> test_simple.cpp
	@echo '}' >> test_simple.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o test_simple test_simple.cpp $(LIBS)
	./test_simple
	@rm -f test_simple test_simple.cpp
	@echo "libclang test passed!"

install: $(TARGET)
	@echo "Installing to /usr/local/bin..."
	sudo cp $(TARGET) /usr/local/bin/
	@echo "Installation complete!"

clean:
	@echo "Cleaning up..."
	rm -f $(TARGET) $(SAMPLE_INPUT) $(SAMPLE_OUTPUT) test_simple test_simple.cpp
	@echo "Clean complete!"

# Debug build
debug: CXXFLAGS += -g -DDEBUG -O0
debug: $(TARGET)

# Verbose build
verbose: CXXFLAGS += -v
verbose: $(TARGET)

help:
	@echo "Available targets:"
	@echo "  all          - Build the loop analyzer (default)"
	@echo "  test         - Build and run with sample code"
	@echo "  test-libclang- Test basic libclang functionality"
	@echo "  clean        - Remove built files"
	@echo "  install      - Install to /usr/local/bin"
	@echo "  debug        - Build debug version"
	@echo "  info         - Show build configuration"
	@echo "  check-deps   - Check if dependencies are installed"
	@echo "  verbose      - Build with verbose output"
	@echo ""
	@echo "Troubleshooting:"
	@echo "  make info          - Show build configuration"
	@echo "  make test-libclang - Test if libclang works"
	@echo "  make verbose       - See detailed compiler output"