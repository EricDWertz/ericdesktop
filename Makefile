ericdesktop: main.c
	gcc -g -o ericdesktop -lX11 -lGL -lGLU -lGLEW -lXxf86vm `pkg-config --cflags --libs gtk+-2.0 gtkglext-1.0 gconf-2.0` main.c
