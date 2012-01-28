states.pdf: states.dot config config.h
	cpp $< | dot -Nwidth=`cpp $< | dot | sed -E -n 's/^.*width="([0-9.]+)".*$$/\1/p' | sort -n | tail -n 1` -T pdf -o $@
