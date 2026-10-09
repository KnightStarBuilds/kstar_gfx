#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace demo {
constexpr float pi = 3.14159265358979323846f;
struct V2 { float x{}, y{}; };
struct V3 { float x{}, y{}, z{}; };
struct V4 { float x{}, y{}, z{}, w{}; };
inline V3 operator+(V3 a,V3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V3 operator-(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V3 operator*(V3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline V3 unit(V3 a){const float n=std::sqrt(dot(a,a));return a*(n>0?1/n:0);}
// Column-major matrices multiply column vectors. World is right-handed, Y up;
// the portable vertex convention uses OpenGL's -1..+1 clip-space depth.
struct M4 { float v[16]{}; };
inline M4 identity(){M4 m{};m.v[0]=m.v[5]=m.v[10]=m.v[15]=1;return m;}
inline M4 operator*(const M4&a,const M4&b){M4 o{};for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)o.v[c*4+r]+=a.v[k*4+r]*b.v[c*4+k];return o;}
inline M4 transpose(const M4&m){M4 o;for(int c=0;c<4;++c)for(int r=0;r<4;++r)o.v[c*4+r]=m.v[r*4+c];return o;}
inline M4 translate(V3 p){M4 m=identity();m.v[12]=p.x;m.v[13]=p.y;m.v[14]=p.z;return m;}
inline M4 scale(V3 s){M4 m=identity();m.v[0]=s.x;m.v[5]=s.y;m.v[10]=s.z;return m;}
inline M4 rotate_y(float a){M4 m=identity();m.v[0]=std::cos(a);m.v[2]=-std::sin(a);m.v[8]=std::sin(a);m.v[10]=std::cos(a);return m;}
inline M4 rotate_x(float a){M4 m=identity();m.v[5]=std::cos(a);m.v[6]=std::sin(a);m.v[9]=-std::sin(a);m.v[10]=std::cos(a);return m;}
inline M4 perspective(float fovy,float aspect,float nearz,float farz){M4 m{};float f=1/std::tan(fovy*.5f);m.v[0]=f/aspect;m.v[5]=f;m.v[10]=(farz+nearz)/(nearz-farz);m.v[11]=-1;m.v[14]=2*farz*nearz/(nearz-farz);return m;}
inline M4 ortho(float left,float right,float bottom,float top,float nearz,float farz){M4 m=identity();m.v[0]=2/(right-left);m.v[5]=2/(top-bottom);m.v[10]=-2/(farz-nearz);m.v[12]=-(right+left)/(right-left);m.v[13]=-(top+bottom)/(top-bottom);m.v[14]=-(farz+nearz)/(farz-nearz);return m;}
inline M4 look_at(V3 eye,V3 target,V3 up){V3 f=unit(target-eye),s=unit(cross(f,up)),u=cross(s,f);M4 m=identity();m.v[0]=s.x;m.v[4]=s.y;m.v[8]=s.z;m.v[1]=u.x;m.v[5]=u.y;m.v[9]=u.z;m.v[2]=-f.x;m.v[6]=-f.y;m.v[10]=-f.z;m.v[12]=-dot(s,eye);m.v[13]=-dot(u,eye);m.v[14]=dot(f,eye);return m;}
inline M4 inverse(const M4&m){
    double a[4][8]{};for(int r=0;r<4;++r){for(int c=0;c<4;++c)a[r][c]=m.v[c*4+r];a[r][r+4]=1;}
    for(int c=0;c<4;++c){int p=c;for(int r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[p][c]))p=r;for(int k=0;k<8;++k)std::swap(a[c][k],a[p][k]);double d=a[c][c];if(std::abs(d)<1e-12)return identity();for(double&x:a[c])x/=d;for(int r=0;r<4;++r)if(r!=c){double s=a[r][c];for(int k=0;k<8;++k)a[r][k]-=s*a[c][k];}}
    M4 o{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)o.v[c*4+r]=float(a[r][c+4]);return o;
}
}
