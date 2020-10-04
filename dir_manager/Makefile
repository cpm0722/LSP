dir_manager: dir_manager.o daemon.o tree.o time.o 
	gcc -o dir_manager dir_manager.o daemon.o tree.o time.o

dir_manager.o : dir_manager.c header.h
	gcc -c dir_manager.c
daemon.o : daemon.c header.h
	gcc -c daemon.c
tree.o : tree.c header.h
	gcc -c tree.c
time.o : time.c header.h
	gcc -c time.c

clean :
	rm -rf *.o log.txt check trash
