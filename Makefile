TASKS := task5

.PHONY: all clean $(TASKS)

all: $(TASKS)

$(TASKS):
	$(MAKE) -C $@

clean:
	for dir in $(TASKS); do \
		$(MAKE) -C $$dir clean; \
	done
