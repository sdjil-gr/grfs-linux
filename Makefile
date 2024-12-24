
DIR_BUILD = ./build
DIR_TOOLS = ./tools
IMAGE = image

SRC = $(wildcard *.c)
SRC_IMAGE = $(wildcard $(DIR_TOOLS)/createimage.c)



all: dirs compile

dirs:
	mkdir -p $(DIR_BUILD)

compile: 
	gcc -g -o $(DIR_BUILD)/file-system $(SRC)
	gcc -g -o $(DIR_BUILD)/createimage $(SRC_IMAGE)

clean:
	rm -rf $(DIR_BUILD)

image:
	$(DIR_BUILD)/createimage

run:
	$(DIR_BUILD)/file-system

.PHONY: all dirs compile clean run