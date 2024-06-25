CC = gcc

libmsocket.a: msocket.o
	ar rcs libmsocket.a msocket.o

msocket.o: msocket.c
	$(CC) -c msocket.c -o msocket.o

initmsocket: initmsocket.c libmsocket.a
	$(CC) initmsocket.c -L. -pthread -o initmsocket

user1: user1.c libmsocket.a
	$(CC) user1.c -L. -lmsocket -pthread -o user1

user2: user2.c libmsocket.a
	$(CC) user2.c -L. -lmsocket -pthread -o user2

user1_2: user1_2.c libmsocket.a
	$(CC) user1_2.c -L. -lmsocket -pthread -o user1_2

user2_2: user2_2.c libmsocket.a
	$(CC) user2_2.c -L. -lmsocket -pthread -o user2_2

clean:
	rm -f *.o *.a user1 user2 initmsocket received_file.txt
	ipcrm -a



