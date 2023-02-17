# Uncanny - CAN-bus Device-Under-Test simulator

sjar=/opt/Synopsys/Defensics/can-bus-1.11.0/testtool/can-bus-1110.jar

all: dut beacon Uncanny.class

distclean: clean
	rm -f dut beacon Uncanny.class

clean:
	rm -rf *~ *.o a.out

dut:	dut.c
	gcc -o dut dut.c

beacon:	beacon.c
	gcc -o beacon beacon.c

Uncanny.class:	Uncanny.java
	javac -classpath ${sjar} -target 1.7 -source 1.7 Uncanny.java
