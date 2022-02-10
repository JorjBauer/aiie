#include <stdio.h>
#include <stdint.h>
#include <math.h>

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#define _565To332(c) ((((c) & 0xe000) >> 8) | (((c) & 0x700) >> 6) | (((c) & 0x18) >> 3))


static void rgb_to_hsv(double r, double g, double b, double *h, double *s, double *v)
{
  // Range for these equations is [0..1] not [0..255]
  r = r / 255.0;
  g = g / 255.0;
  b = b / 255.0;
  
  // h, s, v = hue, saturation, value
  double cmax = max(r, max(g, b)); // maximum of r, g, b
  double cmin = min(r, min(g, b)); // minimum of r, g, b
  double diff = cmax - cmin; // diff of cmax and cmin.
  
  // if cmax and cmax are equal then h = 0
  if (cmax == cmin)
    *h = 0;
  
  // if cmax equal r then compute h
  else if (cmax == r)
    *h = (int)(60 * ((g - b) / diff) + 360) % 360;
  
  // if cmax equal g then compute h
  else if (cmax == g)
    *h = (int)(60 * ((b - r) / diff) + 120) % 360;
  
  // if cmax equal b then compute h
  else if (cmax == b)
    *h = (int)(60 * ((r - g) / diff) + 240) % 360;
  
  // if cmax equal zero
  if (cmax == 0)
    *s = 0;
  else
    *s = (diff / cmax) * 100;
  
  // compute v
  *v = cmax * 100;
}

static void hsv_to_rgb(double H, double S, double V, uint8_t *R, uint8_t *G, uint8_t *B)
{
  float s = S/100;
  float v = V/100;
  float C = s*v;
  float X = C*(1-fabs(fmod(H/60.0, 2)-1));
  float m = v-C;
  float r,g,b;
  if(H >= 0 && H < 60){
    r = C,g = X,b = 0;
  }
  else if(H >= 60 && H < 120){
    r = X,g = C,b = 0;
  }
  else if(H >= 120 && H < 180){
    r = 0,g = C,b = X;
  }
  else if(H >= 180 && H < 240){
    r = 0,g = X,b = C;
  }
  else if(H >= 240 && H < 300){
    r = X,g = 0,b = C;
  }
  else{
    r = C,g = 0,b = X;
  }
  *R = (r+m)*255;
  *G = (g+m)*255;
  *B = (b+m)*255;
}

// blend two 24-bit packed colors
uint32_t blendColors(uint32_t a, uint32_t b)
{
  uint8_t r1, r2, g1, g2, b1, b2;
  r1 = (a & 0xFF0000) >> 16;
  g1 = (a & 0x00FF00) >> 8;
  b1 = (a & 0x0000FF);
  r2 = (b & 0xFF0000) >> 16;
  g2 = (b & 0x00FF00) >> 8;
  b2 = (b & 0x0000FF);

  double h1, s1, v1, h2, s2, v2;
  rgb_to_hsv(r1, g1, b1, &h1, &s1, &v1);
  rgb_to_hsv(r2, g2, b2, &h2, &s2, &v2);
  
  hsv_to_rgb((h1+h2)/2, (s1+s2)/2, (v1+v2)/2, &r1, &g1, &b1);
  uint32_t ret = (r1 << 16) | (g1 << 8) | (b1);

  return ret;
}

// RGB map of each of the lowres colors
const uint8_t loresPixelColors[16][3] = { { 0, 0, 0 }, // black
					  { 0xAC, 0x12, 0x4C }, // magenta
					  { 0x00, 0x07, 0x83 }, // dark blue
					  { 0xAA, 0x1A, 0xD1 }, // purple
					  { 0x00, 0x83, 0x2F }, // dark green
					  { 0x9F, 0x97, 0x7E }, // drak grey
					  { 0x00, 0x8A, 0xB5 }, // medium blue
					  { 0x9F, 0x9E, 0xFF }, // light blue
					  { 0x7A, 0x5F, 0x00 }, // brown
					  { 0xFF, 0x72, 0x47 }, // orange
					  { 0x78, 0x68, 0x7F }, // light gray
					  { 0xFF, 0x7A, 0xCF }, // pink
					  { 0x6F, 0xE6, 0x2C }, // green
					  { 0xFF, 0xF6, 0x7B }, // yellow
					  { 0x6C, 0xEE, 0xB2 }, // aqua
					  { 0xFF, 0xFF, 0xFF } // white
};

static const uint16_t palette16[16] = {
  0x0000, // 0 black                                                            
  0xC006, // 1 magenta                                                          
  0x0010, // 2 dark blue                                                        
  0xA1B5, // 3 purple                                                           
  0x0480, // 4 dark green                                                       
  0x6B4D, // 5 dark grey                                                        
  0x1B9F, // 6 med blue                                                         
  0x0DFD, // 7 light blue                                                       
  0x92A5, // 8 brown                                                            
  0xF8C5, // 9 orange                                                           
  0x9555, // 10 light gray                                                      
  0xFCF2, // 11 pink                                                            
  0x07E0, // 12 green                                                           
  0xFFE0, // 13 yellow                                                          
  0x87F0, // 14 aqua                                                            
  0xFFFF  // 15 white                                                           
};

#define color16To32(x) ( (((x) & 0xF800) << 8) | (((x) & 0x07E0) << 5) | (((x) & 0x001F)<<3) )
#define packColor32(x) ( (x[0] << 16) | (x[1] << 8) | (x[2]) )
#define unpackRed(x) ( ((x) & 0xFF0000) >> 16 )
#define unpackGreen(x) ( ((x) & 0xFF00) >> 8 )
#define unpackBlue(x) ( ((x) & 0xFF) )
// FIXME this blend could be optimized - and it's a dumb blend that
// just averages RGB values individually, rather than trying to find a
// sane blend of 2 pixels. Needs thought.
#define luminanceFromRGB(r,g,b) ( ((r)*0.2126) + ((g)*0.7152) + ((b)*0.0722) )

int main(int argc, char *argv[])
{
  for (int i=0; i<16; i++) {
    printf("color %d: 32-bit 0x%.8X 16-bit 0x%.4X 8-bit 0x%.2X\n",
           i,
           packColor32(loresPixelColors[i]),
           palette16[i],
           _565To332(palette16[i]));
           
  }

  uint32_t mix32[16][16];
  uint16_t mix16[16][16];
  uint8_t mix8[16][16];
  
  for (int a=0; a<16; a++) {
    for (int b=0; b<16; b++) {
      uint32_t v = blendColors(packColor32(loresPixelColors[a]),
                               packColor32(loresPixelColors[b]));
      uint16_t v16 = ((unpackRed(v) & 0xF8) << 8) |
        ((unpackGreen(v) & 0xFC) << 3) |
        ((unpackBlue(v) & 0xF8) >> 3);
      uint8_t v8 = _565To332(v16);
      mix32[a][b] = v;
      mix16[a][b] = v16;
      mix8[a][b] = v8;
    }
  }

  printf("uint8_t mix8[16][16] = {\n");
  for (int a=0; a<16; a++) {
    for (int b=0; b<16; b++) {
      printf("0x%.2X, ", mix8[a][b]);
    }
    printf("\n");
  }
  printf("};\n\n");

  printf("uint16_t mix16[16][16] = {\n");
  for (int a=0; a<16; a++) {
    for (int b=0; b<16; b++) {
      printf("0x%.4X, ", mix16[a][b]);
    }
    printf("\n");
  }
  printf("};\n\n");

  printf("uint32_t mix32[16][16] = {\n");
  for (int a=0; a<16; a++) {
    for (int b=0; b<16; b++) {
      printf("0x%.8X, ", mix32[a][b]);
    }
    printf("\n");
  }
  printf("};\n\n");
  
  return 0;
}

