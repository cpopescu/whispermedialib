/*
  memory requirement in dynamic and static RAM
*/

#ifndef AAC_RAM_H
#define AAC_RAM_H

#include "psy_const.h"

extern __thread float mdctDelayBuffer[MAX_CHANNELS*BLOCK_SWITCHING_OFFSET];

extern __thread int sideInfoTabLong[MAX_SFB_LONG + 1];
extern __thread int sideInfoTabShort[MAX_SFB_SHORT + 1];

extern __thread short *quantSpec;
extern __thread float *expSpec;
extern __thread short *quantSpecTmp;

extern __thread short *scf;
extern __thread unsigned short *maxValueInSfb;

/* dynamic buffers of SBR that can be reused by the core */
extern __thread float sbr_envRBuffer[];
extern __thread float sbr_envIBuffer[];

#endif
