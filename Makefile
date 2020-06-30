all: synth_test.c
	gcc -o synth_test.out synth_test.c -lasound
