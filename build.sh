g++ -Wall `pkg-config --cflags opencv` pir.cpp `pkg-config --libs opencv` -lqpidclient -lqpidmessaging -lpthread -o pir
