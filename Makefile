CXXFLAGS ?= -O3 -std=c++17
OPTIONS ?=

all:
	g++ $(OPTIONS) $(CXXFLAGS) chess.cpp -o chess
