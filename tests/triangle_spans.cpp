#include "../src/TriangleSpans.hpp"
#include <cstdio>
#include <cstdlib>
using Renderer::Detail::SpanPoint;
uint32_t state=0xa7378361;
uint32_t rnd() {state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main() {
    // Unsupported inputs must retain the caller's original solver.
    using Renderer::Detail::TriangleSpans;
    if (TriangleSpans({0,0},{1,1},{2,2},0,1).valid ||
        TriangleSpans({0,0},{0,10},{10,0},0,1).valid ||
        TriangleSpans({0,0},{1<<21,0},{0,10},0,1).valid) return 1;
    // Compare inclusive scanline bounds against independent 64-bit edge
    // functions, including flat tops, vertical edges, clipping and fields.
    uint64_t pixels=0,rows=0;
    for(int trial=0;trial<12000;++trial) {
        int range=trial%2?64:8192, offset=range/2;
        SpanPoint a{(int)(rnd()%range)-offset,(int)(rnd()%range)-offset};
        SpanPoint b{(int)(rnd()%range)-offset,(int)(rnd()%range)-offset};
        SpanPoint c{(int)(rnd()%range)-offset,(int)(rnd()%range)-offset};
        if(trial%7==0) b.y=a.y;
        if(trial%11==0) c.x=b.x;
        if((int64_t)(b.x-a.x)*(c.y-a.y)-(int64_t)(b.y-a.y)*(c.x-a.x)<0) std::swap(b,c);
        const int minX=std::max(0,std::min({a.x,b.x,c.x})&~1);
        const int maxX=std::min(479,std::max({a.x,b.x,c.x})&~1);
        const int minY=std::max(trial%5==0?81:0,std::min({a.y,b.y,c.y})&~1);
        const int maxY=std::min(trial%5==0?238:319,std::max({a.y,b.y,c.y})&~1);
        const int inc=trial%3==0?2:1, step=trial%2?1:2;
        const int start=minY+(inc==2?(trial&1):0);
        Renderer::Detail::TriangleSpans spans(a,b,c,start,inc);
        if(!spans.valid) continue;
        for(int y=start;y<=maxY;++y) {
            int left=1,right=0;
            if(y>=spans.firstY) {
                spans.beginRow(y,inc);
                left=spans.left.x+(spans.left.remainder!=0);right=spans.right.x;
            }
            for(int x=minX;x<=maxX;x+=step) {
                int64_t e0=(int64_t)(b.y-c.y)*(x-c.x)+(int64_t)(c.x-b.x)*(y-c.y);
                int64_t e1=(int64_t)(c.y-a.y)*(x-a.x)+(int64_t)(a.x-c.x)*(y-a.y);
                int64_t e2=(int64_t)(a.y-b.y)*(x-b.x)+(int64_t)(b.x-a.x)*(y-b.y);
                bool expected=e0>=0&&e1>=0&&e2>=0;
                bool actual=x>=left&&x<=right;
                if(actual!=expected) {
                    std::printf("FAIL trial=%d p=%d,%d span=%d..%d a=%d,%d b=%d,%d c=%d,%d\n",trial,x,y,left,right,a.x,a.y,b.x,b.y,c.x,c.y);return 1;
                }
                ++pixels;
            }
            ++rows;
            if(y>=spans.firstY) spans.advance();
            y+=inc-1;
        }
    }
    std::printf("PASS %llu rows, %llu pixel coverage checks\n",(unsigned long long)rows,(unsigned long long)pixels);
}
