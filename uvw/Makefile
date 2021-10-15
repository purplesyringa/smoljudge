all: build/libuv.a build/libuvw.a


../vendor/libuv/configure: ../vendor/libuv/autogen.sh
	cd ../vendor/libuv && sh autogen.sh

../vendor/libuv/Makefile: ../vendor/libuv/configure
	cd ../vendor/libuv && ./configure

../vendor/libuv/.libs/libuv.a: ../vendor/libuv/Makefile
	cd ../vendor/libuv && $(MAKE)

build/libuv.a: ../vendor/libuv/.libs/libuv.a
	cp ../vendor/libuv/.libs/libuv.a build/libuv.a


UVW_CXX_SRCS := $(wildcard ../vendor/uvw/src/uvw/*.cpp)
UVW_CXX_OBJS := $(patsubst ../vendor/uvw/src/uvw/%.cpp,build/uvw/%.o,$(UVW_CXX_SRCS))
UVW_CXX_DEPS := $(patsubst %.o,%.d,$(UVW_CXX_OBJS))

build/libuvw.a: $(UVW_CXX_OBJS)
	$(AR) crf $@ $^

$(UVW_CXX_OBJS): build/uvw/%.o: ../vendor/uvw/src/uvw/%.cpp
	$(CXX) $< -o $@ -g -O2 -std=c++2a -MMD -c -DUVW_AS_LIB -I../vendor/libuv/include

-include $(UVW_CXX_DEPS)


clean:
	$(RM) $(CXX_OBJS) $(CXX_DEPS)
