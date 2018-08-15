# 4d-iod-fast
Fast display functions for 4D systems GEN4-IOD display module (ESP8266+display)

This implement functions to quickly display RLE encoded images from RAM, FLASH or SD card.
a typical 320x240x16bit image takes 5 to 50k and shows in around 100msec (from flash).

It uses the GFX4D arduino library (https://github.com/4dsystems/GFX4d) but low level SPI write functions are used to copy data to the external GRAM of the display.

A Python tool is included to encode the images.
