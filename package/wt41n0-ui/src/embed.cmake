# Embed a file as a C array: cmake -DIN=<file> -DOUT=<c file> -DNAME=<symbol> -P embed.cmake
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
get_filename_component(base "${IN}" NAME)
file(WRITE "${OUT}" "/* generated from ${base} */\n"
	"const unsigned char ${NAME}[] = {${bytes}};\n"
	"const unsigned long ${NAME}_size = sizeof(${NAME});\n")
