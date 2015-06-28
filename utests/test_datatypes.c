#include <stdlib.h>
#include <unistd.h>
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#include <endian.h>
#include <time.h>
#include <math.h>
#include "test_datatypes.h"
#include "streamer.h"
#include "datatypes_common.h"
#include "datatypes.h"

#define RANDOM_FILE 4
#define DO_ERRORS 1000

void recursive_basemod(int m, int n, char *temp)
{
  if (m < n)
    {
      sprintf(temp, "%d", m);
    }
  else
    {
      sprintf(temp, "%d", m % n);
      recursive_basemod(m / n, n, temp + 1);
    }
}

unsigned int get_mask(int start, int end)
{
  unsigned int returnable = 0;
  while (start <= end)
    {
      returnable |= B(start);
      start++;
    }
  return returnable;
}

uint32_t form_hexliteral_from_int(uint32_t m)
{
  int i, j;
  long double powler = 0;
  char temp[33];
  char temp2[2];
  memset(&temp, 0, sizeof(char) * 33);
  memset(&temp2, 0, sizeof(char) * 2);
  //recursive_basemod(m, 10, temp);
  sprintf(temp, "%d", m);

  j = 0;
  for (i = (strlen(temp) - 1); i >= 0; i--)
    {
      memcpy(temp2, temp + i, sizeof(char));
      powler += (atoi(temp2)) * pow(16, j);
      j++;
    }


  return (uint32_t) floor(powler);
}


void set_mark5b_day_and_sec(void *buffer, int sec, int day)
{
  *((uint32_t *) (buffer + 4 + 4)) =
    (form_hexliteral_from_int(day) << 20) |
    form_hexliteral_from_int(sec);
}

void set_mark5b_day_and_sec_from_tms(void *buffer, struct tm *tms)
{
  /*TODO: tm_yday is not correct MJD, but foek it */
  set_mark5b_day_and_sec(buffer, tms->tm_yday,
                         SEC_OF_DAY_FROM_TM(tms));
}

static int testrun(struct opt_s *opt, void *testarea)
{
  uint32_t i, err;
  int *expected_errors = malloc(sizeof(int));
  void *packet;

  D("Initalizing buffer");
  for (i = 0; i < opt->buf_num_elems; i++)
    {
      packet = testarea + i * (opt->packet_size);
      switch (opt->optbits & LOCKER_DATATYPE)
        {
        case DATATYPE_VDIF:
          SET_FRAMENUM_FOR_VDIF(packet, i);
          SET_SECOND_FOR_VDIF(packet, i);
          break;
        case DATATYPE_UDPMON:
          SET_FRAMENUM_FOR_UDPMON(packet, i);
          break;
        case DATATYPE_MARK5BNET:
          SET_FRAMENUM_FOR_MARK5BNET(packet, i);
          break;
        case DATATYPE_MARK5B:
          SET_FRAMENUM_FOR_MARK5B(packet, i);
          //TODO
          break;
        }
      if (i == 0)
        copy_metadata(opt->first_packet, testarea, opt->optbits);
    }

  *expected_errors = 0;
  err = check_and_fill(testarea, opt, 0, expected_errors);
  ck_assert(err == 0);
  if (*expected_errors != 0)
    {
      ck_abort_msg("Found errors when shouldn't!");
      return -1;
    }
  D("Default situation checked ok. Testing file n-situation");
  for (i = 0; i < opt->buf_num_elems; i++)
    {
      packet = testarea + i * (opt->packet_size);
      switch (opt->optbits & LOCKER_DATATYPE)
        {
        case DATATYPE_VDIF:
          SET_FRAMENUM_FOR_VDIF(packet, i);
          SET_SECOND_FOR_VDIF(packet, i);
          //TODO properly
          break;
        case DATATYPE_UDPMON:
          SET_FRAMENUM_FOR_UDPMON(packet,
                                  i + RANDOM_FILE * opt->buf_num_elems);
          break;
        case DATATYPE_MARK5BNET:
          SET_FRAMENUM_FOR_MARK5BNET(packet,
                                     i + RANDOM_FILE * (opt->buf_num_elems));
          break;
        case DATATYPE_MARK5B:
          SET_FRAMENUM_FOR_MARK5B(packet,
                                  i + RANDOM_FILE * (opt->buf_num_elems));
          //TODO
          break;
        }
    }
  *expected_errors = 0;
  err = check_and_fill(testarea, opt, RANDOM_FILE, expected_errors);
  ck_assert(err == 0);
  if (*expected_errors != 0)
    {
      ck_abort_msg("Found errors when shouldn't!");
      return -1;
    }
  D("Doing random writes to produce some errors");
  for (i = 0; i < DO_ERRORS; i++)
    {
      int packetnum = (rand() % (opt->buf_num_elems - 1));
      D("Doing error in packetnum %d", packetnum);
      memset(testarea + packetnum * (opt->packet_size), 0, 32);
    }
  *expected_errors = DO_ERRORS;
  err = check_and_fill(testarea, opt, RANDOM_FILE, expected_errors);
  ck_assert(err == 0);
  if (*expected_errors > 0 && *expected_errors <= DO_ERRORS)
    {
      D("Amount of errors reasonable: %d", *expected_errors);
    }
  else
    {
      ck_abort_msg("Unreasonable amount of errors: %d",
                   *expected_errors);
      return -1;
    }
  D("Checking fixed area");
  *expected_errors = 0;
  err = check_and_fill(testarea, opt, RANDOM_FILE, expected_errors);
  ck_assert(err == 0);
  if (*expected_errors != 0)
    {
      ck_abort_msg("Found errors when shouldn't!");
      return -1;
    }

  void *teststring = NULL;
  switch (opt->optbits & LOCKER_DATATYPE)
    {
    case DATATYPE_VDIF:
      teststring = malloc(HSIZE_VDIF + 4);
      memset(teststring, 0, HSIZE_VDIF + 4);
      D("Not implemented yet");
      return 0;
      //TODO properly
      break;
    case DATATYPE_UDPMON:
      D("Not doing this stuff for udpmon");
      return 0;
      break;
    case DATATYPE_MARK5BNET:
      teststring = malloc(HSIZE_MARK5BNET + 4);
      memset(teststring, 0, HSIZE_MARK5BNET + 4);
      break;
    case DATATYPE_MARK5B:
      teststring = malloc(HSIZE_MARK5B + 4);
      memset(teststring, 0, HSIZE_MARK5B + 4);
      break;
    }
  int sec, day;
  long lerr = 0;
  //uint32_t datthere;
  int temperr = 0, tempdiff;
  TIMERTYPE temptime;
  GETTIME(temptime);
  struct tm gmtime_s;
  gmtime_r(&(temptime.tv_sec), &gmtime_s);
  switch (opt->optbits & LOCKER_DATATYPE)
    {
    case DATATYPE_VDIF:
      lerr = epochtime_from_vdif((void *)teststring, &gmtime_s);
      break;
    case DATATYPE_MARK5BNET:
      //TODO: Disabled for now
      return 0;
      set_mark5b_day_and_sec_from_tms(teststring + 8, &gmtime_s);
      lerr = epochtime_from_mark5b_net((void *)teststring, &gmtime_s);
      if (lerr != NONEVEN_PACKET)
        {
          ck_abort_msg("epochtime from mark5bnet should return NONEVEN_PACKET when no ABADDEED present");
          return -1;
        }
      *((long *)(teststring + 8)) = 0xABADDEED;
      tempdiff =
        secdiff_from_mark5b_net((void *)teststring, &gmtime_s, &temperr);
      if (temperr != 0)
        {
          ck_abort_msg("ERr in getting secdiff");
          return -1;
        }
      if (tempdiff != 0)
        {
          ck_abort_msg("Should get 0 for diff. Got %d", tempdiff);
          return -1;
        }
      lerr = epochtime_from_mark5b_net((void *)teststring, &gmtime_s);
      break;
    case DATATYPE_MARK5B:
      set_mark5b_day_and_sec_from_tms(teststring, &gmtime_s);
      tempdiff = secdiff_from_mark5b((void *)teststring, &gmtime_s, &temperr);
      if (temperr != 0)
        {
          ck_abort_msg("ERr in getting secdiff");
          return -1;
        }
      if (tempdiff != 0)
        {
          ck_abort_msg("Should get 0 for diff");
          return -1;
        }
      lerr = epochtime_from_mark5b((void *)teststring, &gmtime_s);
      break;
    }
  if (lerr != temptime.tv_sec)
    {
      ck_abort_msg("didnt get %ld from epochtime-counter, got %ld",
                   temptime.tv_sec, lerr);
      return -1;
    }
  int temp;
  if (get_sec_dif_from_buf((void *)teststring, &gmtime_s, opt,
                           &temp) != 0)
    {
      ck_abort_msg("didnt get zero from sec dif with time now");
      return -1;
    }
  else if (temp != 0)
    {
      ck_abort_msg("Err in retval");
      return -1;
    }

  for (i = 0; i < 24 * 60 * 60; i++)
    {
      switch (opt->optbits & LOCKER_DATATYPE)
        {
        case DATATYPE_VDIF:
          break;
        case DATATYPE_MARK5BNET:
          sprintf((char *)(teststring + 8 + 4 + 4), "%03d%05d",i % 365, i);
          temp =
            get_sec_and_day_from_mark5b_net((void *)teststring, &sec, &day);
          break;
        case DATATYPE_MARK5B:
          sprintf((char *)(teststring + 4 + 4), "%03d%05d", i % 365, i);
          temp = get_sec_and_day_from_mark5b((void *)teststring, &sec, &day);
          break;
        }
      if (temp != 0)
        {
          ck_abort_msg("Didnt get both sec and day!");
          return -1;
        }
      if (sec != i)
        {
          ck_abort_msg("Got %d for sec when expected %d", sec, i);
          return -1;
        }
      if (day != i % 365)
        {
          ck_abort_msg("Got %d for sec when expected %d", day, i % 365);
          return -1;
        }
    }
  free(expected_errors);
  free(teststring);
  return 0;
}


START_TEST(test_datatypes_udpmon)
{
  struct opt_s opt = { };
  int ret;
#define N_PACKETS 6500
#define PACKET_SIZE 1200
  uint8_t testarea[PACKET_SIZE * N_PACKETS];
  uint8_t first_packet[PACKET_SIZE];

  opt.buf_num_elems = N_PACKETS;
  opt.packet_size = PACKET_SIZE;
  opt.optbits &= ~LOCKER_DATATYPE;
  opt.optbits |= DATATYPE_UDPMON;
  opt.first_packet = first_packet;

  ret = testrun(&opt, testarea);
  ck_assert(ret == 0);
}
END_TEST

START_TEST(test_datatypes_mark5)
{
  struct opt_s opt = { };
  int ret;
#define N_PACKETS 6500
#define PACKET_SIZE 1200
  uint8_t testarea[PACKET_SIZE * N_PACKETS];
  uint8_t first_packet[PACKET_SIZE];

  opt.buf_num_elems = N_PACKETS;
  opt.packet_size = PACKET_SIZE;
  opt.optbits &= ~LOCKER_DATATYPE;
  opt.optbits |= DATATYPE_MARK5BNET;
  opt.first_packet = first_packet;

  ret = testrun(&opt, testarea);
  ck_assert(ret == 0);
}
END_TEST

void add_my_tests(TCase * core)
{
  tcase_add_test(core, test_datatypes_udpmon);
  tcase_add_test(core, test_datatypes_mark5);
}
