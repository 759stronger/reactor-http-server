CXX ?= g++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra
LDLIBS += -pthread
HEADERS := $(wildcard include/reactor/*.hpp)

.PHONY: all manual-tests examples bench clean
all: build/reactor-http-server build/reactor-echo-server

build/reactor-http-server: examples/http/main.cc $(HEADERS)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

build/reactor-echo-server: examples/echo/main.cc $(HEADERS)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

manual-tests: build/manual/http_keep_alive build/manual/http_idle_timeout build/manual/http_incomplete_body build/manual/http_large_upload

build/manual/%: tests/manual/%.cpp $(HEADERS)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

examples: build/examples/tcp_server build/examples/reactor_components build/examples/tcp_client build/examples/any build/examples/timewheel build/examples/socket

build/examples/%: examples/tcp/%.cc $(HEADERS)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

build/examples/%: examples/experiments/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

bench:
	$(MAKE) -C third_party/webbench

clean:
	$(RM) -r build
