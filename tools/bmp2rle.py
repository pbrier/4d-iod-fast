#
# bmp2rle.py
# Convert an 24bit (888RGB) BMP file to a 16bit (565RGB) file to be used on an embedded device (ESP8266 in this case)
# Export as C header file to be included in the software
#
# Copyright (c) 2018, Peter Brier
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# The views and conclusions contained in the software and documentation are those
# of the authors and should not be interpreted as representing official policies,
# either expressed or implied, of the 4D Display project.
#
# Uses imageio and numpy install using "pip install imageio"
#
# Image structure is:
# struct {
#  int32 width, height;
#  unsigned char[] data;
# }
#
# The pixel data is encoded as:
# { 
#   unsigned char repeat; 
#   unsigned short pixels[];
# } 
#
# where: 
#  repeat = 1..127:  copy next 1 to 127 pixels to output
# and   
#  repeat = 128..255: repeat 1 pixel value 'repeat' times to output
# 
#
# It also generates a assets.bin file that can be used on the SD card. This file is a colection of RLE encoded images, 
# prepended with a table of contents that contains the index and length of the images in the file. Multiple image files 
# in a directory structure are read into 1 file.
#
# Each TOC entry has 2 ints:
#
# { 
#   int offset; 
#   int length; 
# }
#
# Where offset is the byte index of the start of the RLE image AFTER the TOC (i.e. the first image starts at offset '0')
# and length is the number of bytes used for the RLE encoded image. An entry with offset=0 and length=0 marks the end of the 
# TOC. The next byte after this entry will be the start of the data of the first image.
#
# This tool can also generate a navigation file. This contains the relation between the images in the directory structure 
# and can be used to drive a user interface to navigate through the images. Each entry in the navigation structure contains
# 4 fields:
# { 
#   int up;
#   int down;
#   int left;
#   int right;
# }
# Each entry is an image index. If it is -1, you cannot navigate in that direction. Otherwise it is in the range 0..N-1 where
# N is the number of images in the assets file. There are N entries in the navigation file (one for each image). The file is 
# filled acording to the directory structure that contains the images. Navigation between images in a same folder is 
# in LEFT to RIGHT or RIGHT to LEFT. If there is an image file AND a folder with the same name as the image (without extension)
# an UP to DOWN and DOWN to UP navigation releation is filled. Lets take this directory structure:
#
# images-\ 
#        | a.bmp
#        | b.bmp
#        | a-\
#            |yes.bmp
#            |no.bmp
#
# This will generate an assets file with 4 images (index 0 to 3) containing [a.bmp, b.bmp, yes.bmp, no.bmp]
# The following navigation structure is generated with 4 entries:
# 0: { up=-1, down= 2, left=-1, right= 1 }  # a.bmp
# 1: { up=-1, down=-1, left= 0, right=-1 }  # b.bmp
# 2: { up= 0, down=-1, left=-1, right= 3 }  # yes.bmp
# 3: { up= 0, down=-1, left= 2, right=-1 }  # no.bmp
#
#
import imageio
import sys
import numpy as np
import struct
import itertools
import os
import glob


def load_image(name): 
    ''' Load image file into array '''
    img = imageio.imread(name)
    return img

    
def rle_encode(l):
    ''' return list of literals with values to copy to output or tupels that contain (repeat, value) '''
    def encode(l):
        return [(len(list(group)),name) for name, group in itertools.groupby(l)]
    def one_uncode(x):
        length,value = x
        if length == 1:
            return value        
        return x    
    return [one_uncode(i) for i in encode(l)]        


    
def encode_image(img): 
    '''  RLE encode with 8 bit prefix. 1..127 = literal copy of next 1..127 pixels, 128..255 = repeat next pixel 1 to 127 times '''
    w = img.shape[1]
    h = img.shape[0]
    i = 0
    nr = w * h
    
    pixels = img.reshape(w*h)                
    rle = rle_encode(pixels) # make RLE encoded list
    
    b = bytearray( struct.pack('<ll',w, h ) ) 
    for n in rle: # convert list to binary format
        if isinstance(n, np.uint16):
            if i == 0:
                b += struct.pack('<B', 0)
                i = len(b)-1
            if b[i] > 126:                
                b += struct.pack('<B', 0)
                i = len(b)-1
            b += struct.pack('<H', n)
            nr -= 1
            b[i] += 1            
        else:            
            count, p = n
            while count > 0:
                c = min(127, count)              
                b += struct.pack('<BH', c+128, p)                
                count -= c
                nr -= c
                i = 0
            
    if nr != 0:
        print("Error in RLE encoding, remaining pixels=", nr)
        return None
    return b

    
def rgb565(rgb): 
    return int(rgb[2] >> 3) | ( (rgb[1] & 0xFC) << 3) | ( (rgb[0] & 0xF8) << 8)

        
def convert_image(img): 
    ''' convert RGB888 to RGB565 '''
    img2 = np.zeros((img.shape[0],img.shape[1]), dtype=np.uint16)
    for x in range(0,img.shape[0]):
        for y in range(0,img.shape[1]):
            img2[x,y] = rgb565(img[x][y])
    return img2

    

def hex(s):
    '''return a formatted list of hex numbers'''
    return ''.join("0x{:02x}{}".format( c, (lambda x: ', \n' if (x+1) % 16 == 0 else ', ')(i)) for i,c in enumerate(s) )

    
def hex_dump_data(data, name='data', attributes='' ):    
    return'''
/*
 * 8 bit RLE encoded image, 16bpp
 * Length: {0} bytes
 * structure: {{ uint32 w, h; unsigned char data[] }} 
 */
const unsigned char data_{2}[] {3} = {{
{1}
}};
'''.format(len(data), hex(data), name, attributes )
    
    
def decode_image_test(data): 
    dim = struct.unpack_from('<ll', data)
    pixels = dim[0] * dim[1]
    print( 'size: ', dim[0], " x ", dim[1], '   pixels: ', pixels, '   data length: ', len(data) )
    i = 8
    while i < len(data):
        prefix = struct.unpack_from('B', data, i)[0]        
        i += 1
        if prefix == 0 or prefix == 128:
            print("Error, invalid prefix value: ", prefix) 
            sys.exit(1)
        if prefix < 128:
            # print('copy: ', prefix)
            i += 2 * (prefix)
            pixels -= prefix            
        else:
            #print('repeat: ', prefix-128)
            i += 2
            pixels -= prefix-128
    if pixels != 0:    
        print('Error: remaining pixels: ', pixels)
        sys.exit(1)

        
def encode_file(filename, output_dir='assets'):

    name = os.path.splitext(filename)[0]
    name = os.path.join(output_dir, name.replace(' ', '_') )
    
    if not os.path.exists(os.path.dirname(name)):
        os.makedirs(os.path.dirname(name))
    
    img = load_image(filename) 
    img = convert_image(img)
    data = encode_image(img)
    decode_image_test(data)
    str = hex_dump_data(data, name,  '__attribute__((section(".irom.text"))) __attribute__((aligned(4)))' )
    with open(name + '.h', "w") as text_file:
        text_file.write(str)
        
    with open(name + '.bin', "wb") as bin_file:
        bin_file.write(data)
    return name

    
def make_include_header(names):
    str = "// assets.h, generated by bmp2rle.py \n"
    for name in names:
        str += '#include "{0}.h"\n'.format(name)
    str += 'const unsigned char * assets[] = {\n'
    
    for name in names:
        str += '  data_'+ name + ',\n'
    str += '};\n\n'
    return str

    
def make_assets(names):
    ''' make a file that contains all the asset files. first a TOC is written with the offset (relative to the end of the TOC)
    and length of each asset as { int, int } 
    the TOC is terminated by a offset and length value of { 0, 0 }, the next byte is the 1st byte of the 1st asset'''

    toc = b''
    data = b''
    offset = 0
    for name in names:
        bin = open(name + '.bin', "rb").read()
        data += bin
        toc += struct.pack('<ll', offset, len(bin) )
        offset += len(bin)
    toc += struct.pack('<ll', 0, 0)
    return toc + data

    
    
def make_navigation(names): 

    def subdir(d):
        return os.path.join( os.path.dirname(d) , os.path.splitext(os.path.basename(d))[0] )

    navigation = []  
    for n in names:    
        navigation += [ { "index": 0, "up": -1, "down":-1, "left": -1, "right": -1, "name":"" } ]     
    
    for i in range(0, len(names)):
        navigation[i]["name"] = names[i]
        navigation[i]["index"] = i
        # print(basename, os.path.dirname(names[i]), os.path.basename(names[i]) )
        for j in range(i+1, len(names)):
            if subdir(names[i]) == os.path.dirname(names[j]):
                #print( names[i], names[j])
                #print("subdir", i, j)
                navigation[i]["down"] = j
                navigation[j]["up"] = i
                break
        for j in range(i+1, len(names)):
            if os.path.dirname(names[i]) == os.path.dirname(names[j]):
                #print( names[i], names[j])
                #print(i,j)
                navigation[i]["right"] = j
                navigation[j]["left"] = i
                navigation[j]["up"] = navigation[i]["up"]
                break  
    # print(str(navigation).replace('}', '}\n'))
    
    b = b''
    for n in navigation:
        b += struct.pack('<llll', n["left"], n["right"], n["up"], n["down"] )  
    return b


def test_assets_file(filename):
    data = open('assets.bin', "rb").read()
    i = 0
    entries = []
    maxlen = 0
    while i < len(data):
        entry = struct.unpack_from('<ll', data, i)
        i += 8
        if entry[1] == 0:
            break
        print(entry)
        maxlen = max(entry[1], maxlen)
        entries += [ entry ]
    print( 'total entries: ', len(entries), ', total size: ', len(data), ', max length: ', maxlen);    
    print( 'remaining bytes: ', len(data) - (entries[-1][0] + entries[-1][1]) - 8*(len(entries)+1) )
    fp = open('assets.bin', "rb")
    for entry in entries:
        fp.seek(entry[0] + 8*(len(entries)+1))
        bin = fp.read(entry[1])
        decode_image_test(bin)
        
 
#
# do the work:
#    


names = []
for file in glob.iglob('./**/*.png', recursive=True):  
    sys.stdout.write("{0: <50}".format(file))
    names += [ encode_file(file) ] 

assets = make_assets(names)
open('assets.bin', "wb").write(assets)

navigation = make_navigation(names)
open('navigation.bin', "wb").write(navigation)

test_assets_file('assets.bin')
hdr = make_include_header(names)
open('assets.h', "w").write(hdr)

