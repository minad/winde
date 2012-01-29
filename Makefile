# WIDTH=`cpp $< | dot | sed -E -n 's/^.*width="([0-9.]+)".*$$/\1/p' | sort -n | tail -n 1`
WIDTH=2.5

all: statemachine.pdf statemachine.png statemachine.svg

statemachine.%: states.dot config config.h
	cpp $< | dot -Nwidth=$(WIDTH) -T $(subst statemachine.,,$@) -o $@

clean:
	rm -f statemachine.pdf statemachine.png statemachine.svg