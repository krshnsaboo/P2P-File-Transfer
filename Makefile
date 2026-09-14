all:
	$(MAKE) -C tracker
	$(MAKE) -C client

clean:
	$(MAKE) -C tracker clean
	$(MAKE) -C client clean

.PHONY: all clean
