
cpu-usage: cpu-usage.c
	gcc -o $@ $<

clean:
	rm -f cpu-usage
