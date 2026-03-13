CXX = g++
CXXFLAGS = -std=c++14 -O2 -Wall

TARGET = code
SOURCES = main.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET) *.o *.db *.idx *.dat

.PHONY: all clean
