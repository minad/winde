states.pdf: states.dot config config.h
	cpp states.dot | dot -T pdf -o states.pdf

