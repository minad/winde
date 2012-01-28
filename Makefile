states.pdf: states.dot.tmp
	dot -Nwidth=`dot $< | sed -E -n 's/^.*width="([0-9.]+)".*$$/\1/p' | sort -n | tail -n 1` -T pdf -o $@ $<

states.dot.tmp: states.dot config config.h
	cpp $< > $@
