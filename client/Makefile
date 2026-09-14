CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -pthread -O2
INCLUDES = -I. -I../common

SRCS = main.cpp client.cpp ../common/network_utils.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = client

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(TARGET) *.o ../common/*.o

.PHONY: all clean
