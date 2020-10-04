rsync: rsync.o
	gcc -o rsync rsync.o

ssu_rsync.o : rsync.c
	gcc -c rsync.c

clean :
	rm -rf *.o log.txt src dst *.txt
