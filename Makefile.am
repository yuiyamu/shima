bin_PROGRAMS = shima
shima_SOURCES = \
    build.c \
    build.h \
    cli.c \
    main.h \
    install.c \
    install.h \
    heliotrope.c \
    heliotrope.h \
    net.c \
    net.h

# we can assume that linking -lz will work, otherwise thats kinda on them 
AM_CFLAGS = -Wextra -Wpedantic
shima_LDFLAGS = -lz
