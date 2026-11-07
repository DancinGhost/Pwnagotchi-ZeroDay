CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2
INCLUDES := -Icore/include -Irender/include
BUILD    := build

.PHONY: all test sim demo web-serve clean

all: $(BUILD)/test_functional_core $(BUILD)/test_painter $(BUILD)/zd_sim

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_functional_core: tests/core/test_functional_core.cpp core/include/zd/*.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Icore/include tests/core/test_functional_core.cpp -o $@

$(BUILD)/test_painter: tests/render/test_painter.cpp core/include/zd/*.hpp render/include/zd/render/*.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/render/test_painter.cpp -o $@

$(BUILD)/zd_sim: platforms/native/main.cpp core/include/zd/*.hpp render/include/zd/render/*.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) platforms/native/main.cpp -o $@

test: $(BUILD)/test_functional_core $(BUILD)/test_painter
	./$(BUILD)/test_functional_core
	./$(BUILD)/test_painter
	node platforms/web/test_web_core.js

sim: $(BUILD)/zd_sim
	./$(BUILD)/zd_sim

demo: $(BUILD)/zd_sim
	./$(BUILD)/zd_sim --demo

web-serve:
	python3 -m http.server 8080 --directory platforms/web

clean:
	rm -rf $(BUILD)
