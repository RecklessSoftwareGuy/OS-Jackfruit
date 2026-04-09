TASKS := task1 task2 task3 task4 task5 task6

.PHONY: all clean $(TASKS)

all: $(TASKS)

$(TASKS):
	$(MAKE) -C $@

clean:
	for dir in $(TASKS); do \
		$(MAKE) -C $$dir clean; \
	done
