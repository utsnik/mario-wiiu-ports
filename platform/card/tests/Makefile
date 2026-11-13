# Host tests for platform/card's transactional save handling.
CXX ?= g++
CXXFLAGS ?= -O2 -g -std=gnu++11 -Wall -Wextra -Werror -fcommon -DSMS_main=main
WORK ?= $(abspath ../../../..)/build-card-test
INCLUDES = -I../../../src -I../../../decomp/libs/dolphin/include

$(WORK)/card_test: card_test.cpp ../card.cpp
	mkdir -p $(WORK)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ card_test.cpp

run: $(WORK)/card_test
	$(WORK)/card_test replace
	$(WORK)/card_test failure
	$(WORK)/card_test recover

clean:
	rm -rf $(WORK)

.PHONY: run clean
