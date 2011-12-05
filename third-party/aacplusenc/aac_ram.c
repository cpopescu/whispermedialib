/*
  memory requirement in dynamic and static RAM
*/

#include <stdio.h>
#include "aac_ram.h"
#include "aacenc.h"
#include "psy_const.h"
#include "FloatFR.h"
#include "sbr_ram.h"

/*
  Static memory areas, must not be overwritten in other sections of the encoder
*/

/* aac Encoder mdct delay buffer */
__thread float mdctDelayBuffer[MAX_CHANNELS*BLOCK_SWITCHING_OFFSET];

/*
  these tables are initialized once at application start
  and are not touched thereafter
*/

__thread int sideInfoTabLong[MAX_SFB_LONG + 1];
__thread int sideInfoTabShort[MAX_SFB_SHORT + 1];

/*
  Dynamic memory areas, might be reused in other algorithm sections,
*/


/* quantized spectrum */ 
__thread short *quantSpec; /* = (short*)PsBuf3; */

/* scratch space for quantization */
__thread float *expSpec; /*  = sbr_envIBuffer; */ /* FRAME_LEN_LONG values */
__thread short *quantSpecTmp; /*  = (short*) &sbr_envIBuffer[FRAME_LEN_LONG]; */

/* scalefactors */ 
__thread short *scf; /* = (short*) &sbr_envIBuffer[2*FRAME_LEN_LONG]; */ /*[MAX_CHANNELS*MAX_GROUPED_SFB];*/

/* max spectral values pre sfb */
__thread unsigned short *maxValueInSfb; /*  = (unsigned short*) &sbr_envIBuffer[2*FRAME_LEN_LONG+MAX_CHANNELS*MAX_GROUPED_SFB]; */ /* [MAX_CHANNELS*MAX_GROUPED_SFB]; */

