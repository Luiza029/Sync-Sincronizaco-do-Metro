sync_car: sync_car.o sync_car_test.o
	gcc -pthread sync_car.o sync_car_test.o -o sync_car

sync_ca.o: sync_car.c sync_car.h
	gcc -pthread -c sync_car.c

sync_car_test.o: sync_car_test.c sync_car.h
	gcc -pthread -c sync_car_test.c

run: sync_car
	./sync_car

clean: 
	rm -f *.o sync_car

.PHONY: run clean