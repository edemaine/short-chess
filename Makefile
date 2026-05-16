CXXFLAGS ?= -O3 -std=c++17 -Wall
OPTIONS ?=

.PHONY: all figures

all:
	g++ $(OPTIONS) $(CXXFLAGS) chess.cpp -o chess

# Figure rebuilds need SVG Tiler:
#   npm install -g svgtiler
#   make figures

figures:
	svgtiler figures
