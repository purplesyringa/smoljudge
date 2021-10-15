all: rpc


../uvw/build/libuv.a ../uvw/build/libuvw.a:
	$(MAKE) -C ../uvw


CXX_SRCS := $(wildcard src/*.cpp)
CXX_OBJS := $(patsubst src/%.cpp,build/%.o,$(CXX_SRCS))
CXX_DEPS := $(patsubst %.o,%.d,$(CXX_OBJS))

rpc: $(CXX_OBJS) ../uvw/build/libuvw.a ../uvw/build/libuv.a
	$(CXX) $^ -o $@ -pthread -ldl

$(CXX_OBJS): build/%.o: src/%.cpp
	$(CXX) $< -o $@ -g -O2 -Wall -std=c++2a -MMD -c -DUVW_AS_LIB -I../vendor/libuv/include -I../vendor/uvw/src -I../vendor/span/include

-include $(CXX_DEPS)


clean:
	$(RM) $(CXX_OBJS) $(CXX_DEPS)
