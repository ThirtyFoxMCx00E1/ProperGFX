// Host test of the LUT decoder (BMP/PNG/TGA/JPG, any N^3 strip/atlas, auto flip). Build: g++ -std=c++17 -I../src64 -I../third_party -I<GLES2 headers> host_lut_test.cpp ../src64/ps_lut.cpp
#include "ps_lut.h"
#include <stdio.h>
#include <vector>
#include <math.h>
// minimal GL stubs (decode path does not call GL)
extern "C" {
void glGenTextures(int, unsigned*){} void glBindTexture(unsigned, unsigned){} void glTexImage2D(unsigned,int,int,int,int,int,unsigned,unsigned,const void*){}
void glTexParameteri(unsigned,unsigned,int){} void glDeleteTextures(int,const unsigned*){} void glActiveTexture(unsigned){} void glPixelStorei(unsigned,int){}
unsigned glGetError(){return 0;} void glGetIntegerv(unsigned,int* p){*p=0;}
}
void PS_Log(const char*, ...) {}
void PS_FileLog(const char*, ...) {}
static std::vector<unsigned char> rd(const char* p){ FILE* f=fopen(p,"rb"); std::vector<unsigned char> v; if(!f) return v; int c; while((c=fgetc(f))!=EOF) v.push_back(c); fclose(f); return v; }
// sample decoded LUT (nearest) at colour, like applyLut
static void look(const std::vector<unsigned char>& rgb, const PSLutInfo& in, float r, float g, float b, float* o){
  int n=in.n, tpr=in.tilesPerRow; int s=(int)roundf(b*(n-1)); int tx=(s%tpr)*n, ty=(s/tpr)*n;
  int x=tx+(int)roundf(r*(n-1)), y=ty+(int)roundf(g*(n-1)); const unsigned char* p=&rgb[(y*in.w+x)*3]; for(int i=0;i<3;i++) o[i]=p[i]/255.f; }
int main(int c, char** v){
  for(int i=1;i<c;i++){ auto d=rd(v[i]); std::vector<unsigned char> rgb; PSLutInfo in; bool ok=LUT_Decode(d.data(), d.size(), rgb, in);
    float o[3]={0}; if(ok) look(rgb,in,0.8f,0.2f,0.6f,o);
    printf("%-60s ok=%d %dx%d n=%d tpr=%d flipped=%d  (0.8,0.2,0.6)->(%.2f %.2f %.2f) %s\n", v[i], ok, in.w,in.h,in.n,in.tilesPerRow,in.flipped,o[0],o[1],o[2], in.error); }
}
