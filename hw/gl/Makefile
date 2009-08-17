.PHONY: all clean

all: libGL.so

clean:
	rm -f *.so *.o parse_gl_h client_stub.c gl_func.h

parse_gl_h: parse_gl_h.c
	$(CC) -g -o $@ $<

client_stub.c: parse_gl_h
	rm -rf temp ; mkdir temp
	cd temp ; mkdir hw; ln -s ../.. hw/gl ; mkdir temp ; cd temp ; ../../$< ; cp $@ ../..
	rm -rf temp

gl_func.h: parse_gl_h
	rm -rf temp ; mkdir temp
	cd temp ; mkdir hw; ln -s ../.. hw/gl ; mkdir temp ; cd temp ; ../../$< ; cp $@ ../..
	rm -rf temp

libGL.so: client_stub.c opengl_client.c gl_func.h
	$(CC) -fPIC -Wall -g -O2 -fno-strict-aliasing opengl_client.c -shared -o $@ -I. -lXext -lX11 -lXfixes -lm -lpthread
