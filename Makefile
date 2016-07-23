.PHONY: all
all : libcrawler.so file_tester web_tester single_thread concurrent_threads

single_thread : single_thread.c libcrawler.so
	gcc -g -L. -lcrawler -lpthread single_thread.c -Wall -Werror -o single_thread

concurrent_threads: concurrent_threads.c libcrawler.so
	gcc -g -L. -lcrawler -lpthread concurrent_threads.c -Wall -Werror -o concurrent_threads 

file_tester : file_tester.c libcrawler.so
	gcc -g -L. -lcrawler -lpthread file_tester.c -Wall -Werror -o file_tester

web_tester : web_tester.c cs537.c libcrawler.so
	gcc -g -L. -lcrawler -lpthread web_tester.c cs537.c -Wall -Werror -o web_tester

libcrawler.so : crawler.c
	gcc -g -fpic -c crawler.c -Wall -Werror -o crawler.o
	gcc -g -shared -o libcrawler.so crawler.o

.PHONY: clean
clean :
	rm -f file_tester web_tester libcrawler.so *.o *~
