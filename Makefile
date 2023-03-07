OUT=alloc
CC=gcc
HOME=/data/data/com.termux/files/home
SRC=./*.c
OBJ=$(SRC:.c=.o)
CFLAGS=-x c -fPIE -fPIC -O3 -ffinite-math-only -Ofast -funroll-loops
GCFLAGS= -g -Og
LFLAGS=-L$(HOME) -lm
INCLUDES=-I../common_header -I.

main:
	$(CC) -o $(HOME)/$(OUT) $(INCLUDES) $(CFLAGS) $(SRC) $(LFLAGS) && chmod +x $(HOME)/$(OUT) && $(OUT)
lib:
	$(CC) -shared -o $(HOME)/lib$(OUT).so -D MEM_LIB $(INCLUDES) $(CFLAGS) $(SRC) $(LFLAGS) && chmod +x $(HOME)/lib$(OUT).so && echo lib$(OUT).so built
debug:
	$(CC) $(GCFLAGS) -o $(HOME)/$(OUT) $(INCLUDES) $(CFLAGS) $(SRC) $(LFLAGS) && chmod +x $(HOME)/$(OUT) && $(OUT)
dblib:
	$(CC) -shared $(GCFLAGS) -D_LIB_ -o $(HOME)/lib$(OUT).so $(INCLUDES) $(CFLAGS) $(SRC) $(LFLAGS) && chmod +x $(HOME)/$(OUT) && echo lib$(OUT).so built
#app:
#	gcc -shared -fPIC -g -Og fish_main.c -o $(HOME)/libfish.so -lm && chmod +x $(HOME)/libfish.so
#all:
#	gcc -o $(HOME)/$(OUT) $(INCLUDES) $(SRC) $(CFLAGS) $(LFLAGS)
#lib:
#	gcc -shared -o $(HOME)/lib$(OUT).so $(INCLUDES) $(SRC) $(CFLAGS) $(LFLAGS)
#gall:
#	gcc -o $(HOME)/$(OUT) $(INCLUDES) $(SRC) $(CFLAGS) $(LFLAGS) -g
#glib:
#	gcc -shared -o $(HOME)/lib$(OUT).so $(INCLUDES) $(SRC) $(CFLAGS) $(LFLAGS) -g
