ceptr: clean specs

specs: ceptr_specs
	./ceptr_specs

ceptr_specs: src/*.h spec/*.c
	gcc -g -o ceptr_specs spec/*.c

clean:
	-rm -rf ceptr_specs *.o ceptr_spec.dSYM
