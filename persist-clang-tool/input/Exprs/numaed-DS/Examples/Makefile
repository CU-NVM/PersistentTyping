CC=clang++
DIRS=Node Stack BinarySearch 
LINK_DIRS=-L../Stack/bin #-L../BinarySearch/bin
LINK_FLAGS=-lnuma

NUMADIR= ../../../../../numaLib/
REALDIR= $(realpath $(NUMADIR))

STATIC_LINK_FLAGS=../Node/bin/libnode.a ../Stack/bin/libstack.a #../BinarySearch/bin/libbinarysearch.a 
INC_DIRS= -I../Node/include -I../Stack/include -I$(REALDIR)   #-I../BinarySearch/include -std=c++17 -I
EXE=./bin/DSExample
OBJS=main.o TestSuite.o

$(EXE): $(OBJS)
	$(CC) $(OBJS)  -o $(EXE) 

$(EXE): $(OBJS)
	$(CC) $(OBJS) $(LINK_FLAGS) -o $(EXE) 

main.o: 
	$(CC) -c -g -D_NODE_HPP=1 main.cpp

TestSuite.o: TestSuite.hpp TestSuite.cpp
	$(CC) -c $(INC_DIRS) -g TestSuite.cpp 

clean:
	rm *.o $(EXE)
