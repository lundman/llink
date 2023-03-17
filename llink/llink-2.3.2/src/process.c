#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#if HAVE_TIME_H
#include <time.h>
#endif

#include "lion.h"

#include "request.h"
#include "process.h"
#include "debug.h"




//
// Convert .BIN files to .MPG.
//
// Read in sectors in size 2352.
// For each sector, skip first 24 bytes,
// then write 2324 bytes. 
// Skip remaining 4.
//
// Do we seek forward to where it probably starts?
//
#define VCD_SECTOR_RAW 2352
#define VCD_SECTOR_HEAD 24
#define VCD_SECTOR_DATA 2324
#define VCD_SECTOR_TAIL 4

//
// We take two pairs of buffer and size. The first one we set to where we
// wish the write to start from, and its size.
// The second is a remembering ptr so we know where to start the next time.
// We also update node->sector_offset;
//
int process_bin2mpg(request_t *node, 
					char **write_from, int *write_size,
					char **last_buf, int *last_size,
					int *sector_offset)
{
	char *in = *last_buf;
	int insize = *last_size, togo;


#if 0
	debugf("x off %d byte %"PRIu64" siz %d\n",
		   *sector_offset,
		   node->bytes_sent,
		   *write_size);
#endif

	// Logic to get us the start of each 2352 sector.

	// Set return size to 0 here incase we need to bail.
	//debugf("[process] in offset %d\n", *sector_offset);

	// node->sector_offset has our current sector offset. When we first start
	// reading this file, it should be 0.

	// Case one, we are currently in the "head" part.
	if (*sector_offset < VCD_SECTOR_HEAD) { // 0 - 23 inclusive.

		togo = VCD_SECTOR_HEAD - *sector_offset;

		*sector_offset += togo;

		// If we have 24(togo) or less bytes, there is nothing to do.
		if (insize <= togo) {
			*sector_offset += insize;
			*last_size = 0;
			*write_size = 0;
				//			debugf("[process] out 0 size %d offset %d\n", insize, *sector_offset);
			return 0;
		}

		// Skip past the 24(togo) bytes.
		in += togo;
		insize -= togo;
		
	
		// We know there is data left, so lets drop down.

	}


	// Case two, we are inside the data area.
	if ((*sector_offset >= VCD_SECTOR_HEAD) &&
		(*sector_offset < VCD_SECTOR_HEAD + VCD_SECTOR_DATA)) { // 24-2347 incl.




		// Do we have enough or less data?
		if (insize <= VCD_SECTOR_DATA) {

			// If we have 2324, or less, we just return that.
			// Assigned the from pointers now.
			*sector_offset += insize;
			*write_size = insize;
			*write_from = in;

			// There is nothing more after this.
			*last_size = 0;

			//			debugf("[process] out 1 size %d offset %d\n", insize, *sector_offset);

			return insize;
			
		} // not more data than needed...

		// Ok we have more than we need...
		*write_from = in;

		// How much data do we have to go to fill this block
		// sector_offset >= 24 as we already know from above.
		togo = VCD_SECTOR_DATA - (*sector_offset - VCD_SECTOR_HEAD);


		*write_size = togo;
		insize -= togo;
		in += togo;
		*sector_offset += togo;

	} // in the data section



	// Case 3, the tail part.
	if (*sector_offset >= (VCD_SECTOR_HEAD + VCD_SECTOR_DATA)) {

		// Can we also skip the tail part?
		if (insize <= VCD_SECTOR_TAIL) {
			*sector_offset += insize;
			*last_size = 0;

			//			debugf("[process] out 2 size %d offset %d\n", *write_size, *sector_offset);
			return *write_size;
		}

		togo = VCD_SECTOR_RAW - *sector_offset;

		in += togo;
		insize -= togo;
		
		*sector_offset += togo;

		// Sanity
		if (*sector_offset != VCD_SECTOR_RAW) {
			debugf("[process] bin2mpg sector_offset incorrect! %d == %d\n",
				   VCD_SECTOR_RAW, *sector_offset);
		}

		*sector_offset = 0;

		// We KNOW there is data left, so lets fix that.
		*last_buf = in;
		*last_size = insize;

		//		debugf("[process] out 3 size %d offset %d\n", *write_size, *sector_offset);
		return *write_size;

	}

	debugf("[process] bin2mpg - i reached the function end?!\n");
	return *write_size;

}



int process_vcd_sector(request_t *node)
{
	return ((int)
			(node->bytes_from + node->bytes_sent) % VCD_SECTOR_RAW);
}


void process_bin2mpg_size(lion64u_t *size)
{
	
	if (!*size) return;

	*size = *size / VCD_SECTOR_RAW * VCD_SECTOR_DATA;

}

