#define O(...) fprintf(stdout, __VA_ARGS__)
#define MARK5SIZE 10016
#define VDIFSIZE 8224
#define MARK5NETSIZE 10032
#define MARK5OFFSET 8
#define BOLPRINT(x) (x)?"true":"false"
#define DATATYPEPRINT(x) (x)?"Comple_x":"Real"
#define GRAB_AND_SHIFT(word,start,end) ((word & get_mask(start,end)) >> start)
#define JUMPSIZE 1000
#define SHIFTCHAR(x) ((((x) & 0x08) >> 3) | (((x) & 0x04) >> 1) | (((x) & 0x02) << 1) | (((x) & 0x01) << 3))
#define B(x) (1 << x)
//#define BITSELECTION(x,...) (1 << x)|BITSELECTION(__VA_ARGS__)
inline unsigned int get_mask(int start, int end){
  unsigned int returnable = 0;
  while(start <= end){
    returnable |= B(start);
    start++;
  }
  return returnable;
}
