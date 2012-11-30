#include "streamer.h"
#include "active_file_index.h"

#define ALL_DONE 3123

//#define AUGMENTLOCK do{if(opt->optbits & (LIVE_SENDING | LIVE_RECEIVING)){pthread_spin_lock(opt->augmentlock);}}while(0)
//#define AUGMENTLOCK FILOCK(opt->fileindex)

//#define AUGMENTUNLOCK do{if(opt->optbits & (LIVE_SENDING | LIVE_RECEIVING)){pthread_spin_unlock(opt->augmentlock);}}while(0)
//#define AUGMENTLOCK FILOCK(opt->fileindex)

struct sender_tracking{
  //unsigned long files_loaded;
  //struct fileholder* head_loaded;
  int allocated_to_load;
  //unsigned long files_sent;
  unsigned long files_skipped;
  unsigned long packets_loaded;
  unsigned long packets_sent;
  unsigned long packetpeek;
  TIMERTYPE now;
#if(SEND_DEBUG)
  TIMERTYPE reference;
#endif
#ifdef UGLY_BUSYLOOP_ON_TIMER
  TIMERTYPE onenano;
#endif
  TIMERTYPE req;
};

void init_sender_tracking(struct opt_s *opt, struct sender_tracking *st);
int start_loading(struct opt_s * opt, struct buffer_entity *be, struct sender_tracking *st);
inline int should_i_be_running(struct opt_s *opt, struct sender_tracking *st);
int loadup_n(struct opt_s *opt, struct sender_tracking * st);
void throttling_count(struct opt_s* opt, struct sender_tracking * st);
int jump_to_next_file(struct opt_s *opt, struct streamer_entity *se, struct sender_tracking *st);
