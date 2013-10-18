g++ -Wall `pkg-config --cflags opencv` pir.cpp `pkg-config --libs opencv` -lqpidclient -lqpidmessaging -lpthread -O3 -mfloat-abi=hard -mfpu=vfp -o pir
