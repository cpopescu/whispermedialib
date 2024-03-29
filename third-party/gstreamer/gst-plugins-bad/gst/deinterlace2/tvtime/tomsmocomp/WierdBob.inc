// -*- c++ -*-

		// First, get and save our possible Bob values
		// Assume our pixels are layed out as follows with x the calc'd bob value
		// and the other pixels are from the current field
		//  
		//        j a b c k		current field
		//            x			calculated line
		//        m d e f n		current field
		//
		// we calc the bob value as:
		//		x2 = either avg(a,f), avg(c,d), avg(b,e), avg(j,n), or avg(k,m)
		 
		// selected for the	smallest of abs(a,f), abs(c,d), or abs(b,e), etc.

		// a,f
		"movq    -2(%%"XBX"), %%mm0\n\t"		// value a from top left		
		"movq    2(%%"XBX", %%"XCX"), %%mm1\n\t"	// value f from bottom right			
		"movq	%%mm0, %%mm6\n\t"					
//		pavgb	%%mm6, %%mm1					// avg(a,f), also best so far
		V_PAVGB ("%%mm6", "%%mm1", "%%mm7", _ShiftMask)	// avg(a,f), also best so far
        "movq	%%mm0, %%mm7\n\t"
		"psubusb	 %%mm1, %%mm7\n\t"
		"psubusb %%mm0, %%mm1\n\t"
		"por		%%mm1, %%mm7\n\t"					// abs diff, also best so far

		// c,d
		"movq    2(%%"XBX"), %%mm0\n\t"		// value a from top left		
		"movq    -2(%%"XBX", %%"XCX"), %%mm1\n\t"	// value f from bottom right			
		"movq	%%mm0, %%mm2\n\t"						
//		pavgb	%%mm2, %%mm1					// avg(c,d)
		V_PAVGB ("%%mm2", "%%mm1", "%%mm3", _ShiftMask)	// avg(c,d)
        "movq	%%mm0, %%mm3\n\t"
		"psubusb	%%mm1, %%mm3\n\t"
		"psubusb %%mm0, %%mm1\n\t"
		"por		%%mm1, %%mm3\n\t"					// abs(c,d)
		"movq	%%mm3, %%mm1\n\t"					// keep copy

		"psubusb %%mm7, %%mm3\n\t"			// nonzero where new weights bigger, else 0
		"pxor	%%mm4, %%mm4\n\t"
		"pcmpeqb %%mm4, %%mm3\n\t"			// now ff where new better, else 00	
		"pcmpeqb	%%mm3, %%mm4\n\t"			// here ff where old better, else 00

		"pand	%%mm3, %%mm1\n\t"			// keep only better new avg and abs
		"pand	%%mm3, %%mm2\n\t"

		"pand	%%mm4, %%mm6\n\t"
		"pand    %%mm4, %%mm7\n\t"

		"por		%%mm2, %%mm6\n\t"			// and merge new & old vals keeping best
		"por		%%mm1, %%mm7\n\t"
		"por		"_UVMask", %%mm7\n\t"			// but we know chroma is worthless so far
		"pand	"_YMask", %%mm5\n\t"			// mask out chroma from here also

		// j,n
		"movq    -4(%%"XBX"), %%mm0\n\t"		// value j from top left		
		"movq    4(%%"XBX", %%"XCX"), %%mm1\n\t"	// value n from bottom right			
		"movq	%%mm0, %%mm2\n\t"						
//		pavgb	%%mm2, %%mm1					// avg(j,n)
		V_PAVGB ("%%mm2", "%%mm1", "%%mm3", _ShiftMask)	// avg(j,n)
        "movq	%%mm0, %%mm3\n\t"
		"psubusb	%%mm1, %%mm3\n\t"
		"psubusb %%mm0, %%mm1\n\t"
		"por		%%mm1, %%mm3\n\t"					// abs(j-n)
		"movq	%%mm3, %%mm1\n\t"					// keep copy

		"psubusb %%mm7, %%mm3\n\t"			// nonzero where new weights bigger, else 0
		"pxor	%%mm4, %%mm4\n\t"
		"pcmpeqb %%mm4, %%mm3\n\t"			// now ff where new better, else 00	
		"pcmpeqb	%%mm3, %%mm4\n\t"			// here ff where old better, else 00

		"pand	%%mm3, %%mm1\n\t"			// keep only better new avg and abs
		"pand	%%mm2, %%mm3\n\t"

		"pand	%%mm4, %%mm6\n\t"
		"pand    %%mm4, %%mm7\n\t"

		"por		%%mm3, %%mm6\n\t"			// and merge new & old vals keeping best
		"por		%%mm1, %%mm7\n\t"			// "

		// k, m
		"movq    4(%%"XBX"), %%mm0\n\t"		// value k from top right		
		"movq    -4(%%"XBX", %%"XCX"), %%mm1\n\t"	// value n from bottom left			
		"movq	%%mm0, %%mm4\n\t"						
//		pavgb	%%mm4, %%mm1					// avg(k,m)
		V_PAVGB ("%%mm4", "%%mm1", "%%mm3", _ShiftMask)	// avg(k,m)

        "movq	%%mm0, %%mm3\n\t"
		"psubusb	%%mm1, %%mm3\n\t"
		"psubusb %%mm0, %%mm1\n\t"
		"por		%%mm1, %%mm3\n\t"					// abs(k,m)
		"movq	%%mm3, %%mm1\n\t"					// keep copy
            
		"movq	%%mm4, %%mm2\n\t"			// avg(k,m)

		"psubusb %%mm7, %%mm3\n\t"			// nonzero where new weights bigger, else 0
		"pxor	%%mm4, %%mm4\n\t"
		"pcmpeqb %%mm4, %%mm3\n\t"			// now ff where new better, else 00	
		"pcmpeqb	%%mm3, %%mm4\n\t"			// here ff where old better, else 00

		"pand	%%mm3, %%mm1\n\t"			// keep only better new avg and abs
		"pand	%%mm2, %%mm3\n\t"
            
		"pand	%%mm4, %%mm6\n\t"
		"pand    %%mm4, %%mm7\n\t"

		"por		%%mm3, %%mm6\n\t"			// and merge new & old vals keeping best
		"por		%%mm1, %%mm7\n\t"			// "

		// b,e
		"movq    (%%"XBX"), %%mm0\n\t"		// value b from top 		
		"movq    (%%"XBX", %%"XCX"), %%mm1\n\t"	// value e from bottom 

// We will also calc here the max/min values to later limit comb
// so the max excursion will not exceed the Max_Comb constant

#ifdef SKIP_SEARCH		
		"movq	%%mm0, %%mm2\n\t"
//		pminub	%%mm2, %%mm1
		V_PMINUB ("%%mm2", "%%mm1", "%%mm4")

//		pmaxub	%%mm6, %%mm2			// clip our current results so far to be above this
		V_PMAXUB ("%%mm6", "%%mm2")
		"movq	%%mm0, %%mm2\n\t"
		V_PMAXUB ("%%mm2", "%%mm1")
//		pminub	%%mm6, %%mm2			// clip our current results so far to be below this
		V_PMINUB ("%%mm6", "%%mm2", "%%mm4")

#else
        "movq	%%mm0, %%mm2\n\t"
		"movq	(%%"XAX"), %%mm4\n\t"
		"psubusb %%mm4, %%mm2\n\t"
		"psubusb %%mm0, %%mm4\n\t"
		"por		%%mm2, %%mm4\n\t"			// abs diff
		
		"movq	%%mm1, %%mm2\n\t"
		"movq	(%%"XAX", %%"XCX"), %%mm3\n\t"
		"psubusb %%mm3, %%mm2\n\t"
		"psubusb %%mm1, %%mm3\n\t"
		"por		%%mm2, %%mm3\n\t"			// abs diff
//		pmaxub  %%mm3, %%mm4			// top or bottom pixel moved most
		V_PMAXUB ("%%mm3", "%%mm4")			// top or bottom pixel moved most
        "psubusb "_Max_Mov", %%mm3\n\t"		// moved more than allowed? or goes to 0?
		"pxor	%%mm4, %%mm4\n\t"
		"pcmpeqb %%mm4, %%mm3\n\t"			// now ff where low motion, else high motion
		
		"movq	%%mm0, %%mm2\n\t"
//		pminub	%%mm2, %%mm1
		V_PMINUB ("%%mm2", "%%mm1", "%%mm4")

//		pmaxub	%%mm6, %%mm2			// clip our current results so far to be above this
		V_PMAXUB ("%%mm6", "%%mm2")

		"psubusb %%mm3, %%mm2\n\t"			// maybe decrease it to 0000.. if no surround motion
		"movq	%%mm2, "_Min_Vals"\n\t"

		"movq	%%mm0, %%mm2\n\t"
		V_PMAXUB ("%%mm2", "%%mm1")
//		pminub	%%mm6, %%mm2			// clip our current results so far to be below this
		V_PMINUB ("%%mm6", "%%mm2", "%%mm4")
        "paddusb %%mm3, %%mm2\n\t"			// maybe increase it to ffffff if no surround motion
		"movq	%%mm2, "_Max_Vals"\n\t"
#endif
        
		"movq	%%mm0, %%mm2\n\t"						
//		pavgb	%%mm2, %%mm1					// avg(b,e)
		V_PAVGB ("%%mm2", "%%mm1", "%%mm3", _ShiftMask)	// avg(b,e)
				
        "movq	%%mm0, %%mm3\n\t"
		"psubusb	%%mm1, %%mm3\n\t"
		"psubusb %%mm0, %%mm1\n\t"
		"por		%%mm1, %%mm3\n\t"			// abs(c,d)
		"movq	%%mm3, %%mm1\n\t"			// keep copy of diffs

		"pxor	%%mm4, %%mm4\n\t"			
		"psubusb %%mm7, %%mm3\n\t"			// nonzero where new weights bigger, else 0
		"pcmpeqb %%mm4, %%mm3\n\t"			// now ff where new better, else 00	
		"pcmpeqb	%%mm3, %%mm4\n\t"			// here ff where old better, else 00

		"pand	%%mm3, %%mm1\n\t"
		"pand	%%mm3, %%mm2\n\t"

		"pand    %%mm4, %%mm6\n\t"
		"pand    %%mm4, %%mm7\n\t"

		"por		%%mm2, %%mm6\n\t"			// our x2 value
		"por		%%mm1, %%mm7\n\t"			// our x2 diffs
		"movq	%%mm7, %%mm4\n\t"			// save as bob uncertainty indicator

