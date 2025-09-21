build: Udith/ThreadedServer.cpp Udith/AsyncClient.cpp
	g++ Udith/ThreadedServer.cpp -o ThreadedServer
	g++ Udith/AsyncClient.cpp -o AsyncClient
	
ThreadedServer: Udith/ThreadedServer.cpp
	g++ Udith/ThreadedServer.cpp -o ThreadedServer

AsyncClient: Udith/AsyncClient.cpp
	g++ Udith/AsyncClient.cpp -o AsyncClient
