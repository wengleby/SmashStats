CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra $(shell wx-config --cxxflags)
LDFLAGS = $(shell wx-config --libs) -lsqlite3

# Source files
SRC_DIR = src
SRC = $(SRC_DIR)/main.cpp $(SRC_DIR)/smash_app.cpp $(SRC_DIR)/backend.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = SmashStats_P3.exe

# Default target
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: all clean
