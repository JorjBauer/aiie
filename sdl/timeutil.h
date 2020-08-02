#include <time.h>
#include <mach/mach_time.h>
// Derived from
// http://stackoverflow.com/questions/5167269/clock-gettime-alternative-in-mac-os-x

#define ORWL_NANO (+1.0E-9)
#define ORWL_GIGA UINT64_C(1000000000)
#define NANOSECONDS_PER_SECOND 1000000000UL
#define CYCLES_PER_SECOND g_speed

static double orwl_timebase = 0.0;
static uint64_t orwl_timestart = 0;
static void _init_darwin_shim(void) {
  mach_timebase_info_data_t tb = { 0 };
  mach_timebase_info(&tb);
  orwl_timebase = tb.numer;
  orwl_timebase /= tb.denom;
  orwl_timestart = mach_absolute_time();
}

static int do_gettime(struct timespec *tp) {
  double diff = (mach_absolute_time() - orwl_timestart) * orwl_timebase;
  tp->tv_sec = diff * ORWL_NANO;
  tp->tv_nsec = diff - (tp->tv_sec * ORWL_GIGA);
  return 0;
}

// adds the number of nanoseconds that 'cycles' takes to *start and
// returns it in *out
static void timespec_add_cycles(struct timespec *start,
			 int64_t cycles,
			 struct timespec *out)
{
  out->tv_sec = start->tv_sec;
  out->tv_nsec = start->tv_nsec;

  uint64_t nanosToAdd = (double)((double)cycles * (double) (NANOSECONDS_PER_SECOND) / (double)CYCLES_PER_SECOND);

  out->tv_sec += (nanosToAdd / NANOSECONDS_PER_SECOND);
  out->tv_nsec += (nanosToAdd % NANOSECONDS_PER_SECOND);
  
  if (out->tv_nsec >= NANOSECONDS_PER_SECOND) {
    out->tv_sec++ ;
    out->tv_nsec -= NANOSECONDS_PER_SECOND;
  }
}

static unsigned long cycles_since_time(struct timespec *start)
{
  unsigned long ret = start->tv_sec * CYCLES_PER_SECOND;
  ret += (double)((double)start->tv_nsec * (double)0.001023 + (double) 0.01); // 0.01 for rounding error; one cycle ~= 977517nS, and 977517 * .000001023 is only 0.999999891.
  return ret;
}

// adds the number of microseconds given to *start and 
// returns it in *out
static void timespec_add_us(struct timespec *start,
			 uint64_t micros,
			 struct timespec *out)
{
  out->tv_sec = start->tv_sec;
  out->tv_nsec = start->tv_nsec;

  uint64_t nanosToAdd = micros * 1000L;
  out->tv_sec += (nanosToAdd / NANOSECONDS_PER_SECOND);
  out->tv_nsec += (nanosToAdd % NANOSECONDS_PER_SECOND);
  
  if (out->tv_nsec >= 1000000000L) {
    out->tv_sec++ ;
    out->tv_nsec -= 1000000000L;
  }
}

static void timespec_diff(struct timespec *start, 
		   struct timespec *end,
		   struct timespec *diff,
		   bool *negative) {
  struct timespec t;

  if (negative)
    {
      *negative = false;
    }

  // if start > end, swizzle...                                                                                                                                                              
  if ( (start->tv_sec > end->tv_sec) || ((start->tv_sec == end->tv_sec) && (start->tv_nsec > end->tv_nsec)) )
    {
      t=*start;
      *start=*end;
      *end=t;
      if (negative)
        {
	  *negative = true;
        }
    }

  // assuming time_t is signed ...
  if (end->tv_nsec < start->tv_nsec)
    {
      t.tv_sec  = end->tv_sec - start->tv_sec - 1;
      t.tv_nsec = 1000000000 + end->tv_nsec - start->tv_nsec;
    }
  else
    {
      t.tv_sec  = end->tv_sec  - start->tv_sec;
      t.tv_nsec = end->tv_nsec - start->tv_nsec;
    }

  diff->tv_sec = t.tv_sec;
  diff->tv_nsec = t.tv_nsec;
}

// tsCompare: return -1, 0, 1 for (a < b), (a == b), (a > b)
static int8_t tsCompare(struct timespec *A, struct timespec *B)
{
  if (A->tv_sec < B->tv_sec)
    return -1;

  if (A->tv_sec > B->tv_sec)
    return 1;

  if (A->tv_nsec < B->tv_nsec)
    return -1;

  if (A->tv_nsec > B->tv_nsec)
    return 1;

  return 0;
}

// return time1 - time2. If time1 <= time2, then return 0.
static struct timespec tsSubtract(struct timespec time1, struct timespec time2)
{
  struct timespec result;
  if ((time1.tv_sec < time2.tv_sec) ||
      ((time1.tv_sec == time2.tv_sec) &&
       (time1.tv_nsec <= time2.tv_nsec))) {/* TIME1 <= TIME2? */
    result.tv_sec = result.tv_nsec = 0 ;
  } else {/* TIME1 > TIME2 */
    result.tv_sec = time1.tv_sec - time2.tv_sec ;
    if (time1.tv_nsec < time2.tv_nsec) {
      result.tv_nsec = time1.tv_nsec + 1000000000L - time2.tv_nsec ;
      result.tv_sec-- ;/* Borrow a second. */
    } else {
      result.tv_nsec = time1.tv_nsec - time2.tv_nsec ;
    }
  }

  return (result) ;
}

