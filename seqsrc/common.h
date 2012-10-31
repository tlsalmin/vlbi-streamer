#define O(...) fprintf(stdout, __VA_ARGS__)
#define MARK5SIZE 10016
#define MARK5NETSIZE 10032
#define MARK5OFFSET 8
#define BOLPRINT(x) (x)?"true":"false"
#define DATATYPEPRINT(x) (x)?"Comple_x":"Real"
#define GRAB_AND_SHIFT(word,start,end) ((word & get_mask(start,end)) >> start)
#define JUMPSIZE 1000
#define B(x) (1 << x)
#define BITSELECTION(x,...) (1 << x)|BITSELECTION(__VA_ARGS__)
inline int get_mask(int start, int end){
  int returnable = 0;
  while(start <= end){
    returnable |= B(start);
    start++;
  }
  return returnable;
}
