CXXFLAGS ?= -O3 -std=c++17 -Wall
OPTIONS ?=

all:
	g++ $(OPTIONS) $(CXXFLAGS) chess.cpp -o chess
