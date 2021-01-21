router : router.o

	g++ router.o -o router

router.o : router.cpp router.h
	g++ -c router.cpp

