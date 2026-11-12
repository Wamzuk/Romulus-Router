# Simple Makefile alternative to CMake.
# Usage:
#   make            # build the server (optimised)
#   make run        # build and run on http://127.0.0.1:8080
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDFLAGS  ?= -pthread

TARGET   := romulus_router
SOURCES  := main.cpp
HEADERS  := json.hpp geo.hpp graph.hpp http.hpp places.hpp

# The HTML page lives one directory up from server/.
HTML     := ../Map\ Viewer.html
PORT     ?= 8080

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

.PHONY: run clean
run: $(TARGET)
	./$(TARGET) --port $(PORT) --html $(HTML)

clean:
	rm -f $(TARGET)