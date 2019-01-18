CC = gcc
CFLAGS  = -Wall
TARGET_1 = server
TARGET_2 = client

all: $(TARGET_1) $(TARGET_2)

$(TARGET_1): $(TARGET_1).c
	$(CC) -o $(TARGET_1) $(TARGET_1).c $(CFLAGS) -lsqlite3 -pthread

$(TARGET_2): $(TARGET_2).c
	$(CC) -o $(TARGET_2) $(TARGET_2).c $(CFLAGS) `pkg-config --libs --cflags gtk+-3.0`

clean:
	$(RM) $(TARGET_1) $(TARGET_2) *.o a.out